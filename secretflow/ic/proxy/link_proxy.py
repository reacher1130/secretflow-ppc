# Copyright 2023 Ant Group Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import logging
from secretflow.ic import audit
from typing import Any, Dict

import spu.libspu.link as link

from secretflow.ic.proxy.serializer import deserialize, serialize


class LinkProxy:
    self_party = None
    all_parties = None

    _link = None
    _parties_rank = None
    _addresses = None
    _advertised_addresses = None
    _send_count = 0
    _recv_count = 0

    @classmethod
    def init(
        cls,
        addresses: Dict,
        self_party: str,
        advertised_addresses: Dict = None,
    ):
        if self_party not in addresses:
            raise ValueError(f'unknown self party {self_party!r}')

        cls._parties_rank = {party: i for i, party in enumerate(addresses)}
        cls.self_party = self_party
        cls.all_parties = list(addresses.keys())
        cls._addresses = dict(addresses)
        cls._advertised_addresses = dict(advertised_addresses or addresses)
        cls._send_count = 0
        cls._recv_count = 0

        peers = ', '.join(
            f'{party}={addr}'
            for party, addr in addresses.items()
            if party != self_party
        )
        logging.info(
            'BFIA协议网络初始化：本方=%s，本方监听端点=%s，本方对外端点=%s，对端端点=%s，传输=h2:grpc',
            self_party,
            addresses[self_party],
            cls._advertised_addresses[self_party],
            peers or '无',
        )

        desc = link.Desc()
        for party, addr in addresses.items():
            desc.add_party(party, addr)

        # link configurations for interconnection mode
        desc.brpc_channel_protocol = "h2:grpc"
        desc.throttle_window_size = 0
        desc.disable_msg_seq_id = True
        desc.http_max_payload_size = 2 * 1024 * 1024 * 1024 - 2  # yacl link limit

        self_rank = cls.all_parties.index(self_party)

        cls._link = link.create_brpc(desc, self_rank)
        logging.info(
            'BFIA协议链路对象创建完成：本方=%s，等待与对端建立真实网络连接',
            self_party,
        )

    @classmethod
    def send_raw(cls, dest_party: str, msg_bytes: bytes):
        rank = cls._parties_rank[dest_party]
        cls._link.send_async(rank, msg_bytes)
        cls._send_count += 1
        audit.message("send_queued",cls.self_party,dest_party,msg_bytes)
        logging.info(
            'BFIA协议实际发送：%s(%s) -> %s(%s)，序号=%d，字节数=%d',
            cls.self_party,
            cls._advertised_addresses.get(cls.self_party, 'unknown'),
            dest_party,
            cls._advertised_addresses.get(dest_party, 'unknown'),
            cls._send_count,
            len(msg_bytes),
        )

    @classmethod
    def recv_raw(cls, src_party: str) -> bytes:
        rank = cls._parties_rank[src_party]
        msg_bytes = cls._link.recv(rank)
        cls._recv_count += 1
        audit.message("received",src_party,cls.self_party,msg_bytes)
        logging.info(
            'BFIA协议实际接收：%s(%s) <- %s(%s)，序号=%d，字节数=%d',
            cls.self_party,
            cls._advertised_addresses.get(cls.self_party, 'unknown'),
            src_party,
            cls._advertised_addresses.get(src_party, 'unknown'),
            cls._recv_count,
            len(msg_bytes),
        )
        return msg_bytes

    @classmethod
    def send(cls, dest_party: str, data: Any):
        msg_bytes = serialize(data)
        cls.send_raw(dest_party, msg_bytes)
        logging.debug(f'send type {type(data)} to {dest_party}')

    @classmethod
    def recv(cls, src_party: str) -> Any:
        msg_bytes = cls.recv_raw(src_party)
        data = deserialize(msg_bytes)
        logging.debug(f'recv type {type(data)} from {src_party}')
        return data

    @classmethod
    def stats(cls) -> Dict[str, int]:
        return {'sent': cls._send_count, 'received': cls._recv_count}

    @classmethod
    def stop(cls):
        if cls._link is None:
            return
        logging.info(
            'BFIA协议链路关闭：本方=%s，实际发送=%d条，实际接收=%d条',
            cls.self_party,
            cls._send_count,
            cls._recv_count,
        )
        cls._link.stop_link()
        cls._link = None
        audit.event("transport_closed",party=cls.self_party, sent=cls._send_count, received=cls._recv_count)
