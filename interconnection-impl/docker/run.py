import asyncio
import requests
import json
import os
import subprocess

from typing import Callable

ERROR_TAGS = [
    b"run failed:",
    b"run psi failed:"
]


class LogTail(object):
    def __init__(self, io, exit_condition: Callable[[], bool], check_interval: float = 1):
        self.should_exit = exit_condition
        self.io = io
        self.check_interval = check_interval
        self.is_failed = False

    async def iter_line(self):
        while not self.should_exit():
            line = self.io.readline()
            if line:
                yield line.strip()
            else:
                await asyncio.sleep(self.check_interval)


async def catch_logs(line):
    try:
        print(line.decode("utf-8"))
        for tag in ERROR_TAGS:
            if tag in line:
                return True
    except Exception as e: # noqa
        return False


def proc_exited(proc):
    return proc.poll() is not None


async def log_handler(log_tail, name):
    async for line in log_tail.iter_line():
        if line:
            is_failed = await catch_logs(line)
            if is_failed:
                log_tail.is_failed = True


async def run() -> int:
    # SS 等算法需握手协商 feature_nums；ECDH 在 disable_handshake 下也可运行
    algo = os.getenv("runtime.component.parameter.algo", "")
    disable_hs = "1" if algo in ("ecdh_psi", "psi", "ecdh") else "0"

    # 构建 ic_main 命令行参数
    args = [f"-disable_handshake={disable_hs}"]

    # 传递 has_label 给 SS-LR（gflags DEFINE_bool，需通过命令行传递）
    # has_label 在 common 参数中会被注入到双方节点，但只有 label_owner 才真正持有标签列
    # 因此根据 config.self_role 是否等于 label_owner 动态判断
    self_role = os.getenv("config.self_role", "")
    label_owner = os.getenv("runtime.component.parameter.label_owner", "")
    has_label = "true" if (self_role and label_owner and self_role == label_owner) else "false"
    args.append(f"-has_label={has_label}")

    cmdline = [
        "/bin/bash", "-c", f"/app/ic_main {' '.join(args)}"
    ]

    envs = os.environ.copy()
    for k, v in envs.items():
        print(f"{k} -> {v}")

    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=envs)

    log_tail_out = LogTail(proc.stdout, lambda: proc_exited(proc))
    log_tail_err = LogTail(proc.stderr, lambda: proc_exited(proc))

    await catch_logs(b"----- PSI LOG START -----")

    await asyncio.gather(
        log_handler(log_tail_out, "log_tail_out"),
        log_handler(log_tail_err, "log_tail_err")
    )

    await catch_logs(b"----- PSI LOG END -----")

    code = proc.returncode
    if log_tail_out.is_failed or log_tail_err.is_failed:
        code = 2

    return code


def callback(exit_code: int):
    headers = {'Content-type': 'application/json'}
    callback_url = os.getenv("system.callback")
    task_id = os.getenv("config.task_id")
    role = os.getenv("config.self_role")

    if not callback_url:
        print(f"system.callback not defined! code: {exit_code}")
        return

    status = "SUCCESS" if exit_code == 0 else "FAILED"
    data = {
        "task_id": task_id,
        "status": status,
        "role": role
    }

    try:
        resp = requests.post(callback_url, verify=False, headers=headers, data=json.dumps(data))
        resp_code = resp.status_code

        if resp_code in [200, 0]:
            print(f"callback success, exec code: {exit_code}, call {callback_url} with data {json.dumps(data)} and got status code: {resp_code}")
        else:
            print(f"callback failed!, exec code: {exit_code}, call {callback_url} with data {json.dumps(data)} but got status code: {resp_code}")

    except Exception as e:
        print(f"callback post failed, {e}")


if __name__ == '__main__':
    process_code = asyncio.run(run())
    print(f"process exit with code: {process_code}")
    callback(process_code)
    exit(process_code)
