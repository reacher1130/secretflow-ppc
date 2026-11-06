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

import abc
import logging
from secretflow.ic import audit
from google.protobuf.json_format import MessageToDict
from typing import List, Tuple

from interconnection.common.header_pb2 import OK
from interconnection.handshake.entry_pb2 import HandshakeRequest, HandshakeResponse

from secretflow.ic.proxy import LinkProxy


class IcHandler(abc.ABC):
    def __init__(self, dataset):
        self._dataset = dataset

    def run(self):
        audit.stage("handshake")
        audit.event("start",party=LinkProxy.self_party)
        logging.info(
            'BFIA协议握手开始：本方=%s，参与方=%s',
            LinkProxy.self_party,
            ','.join(LinkProxy.all_parties),
        )
        if not self._handshake():
            logging.warning('BFIA协议握手失败：互联互通链路未完成参数协商')
            audit.event('failed',party=LinkProxy.self_party)
            raise RuntimeError('BFIA handshake refused')
        audit.event('complete',party=LinkProxy.self_party)
        logging.info('BFIA协议握手完成：算法和协议族参数协商成功')
        audit.stage("compute")
        audit.event("start",party=LinkProxy.self_party)
        try:
            result = self._run_algo()
        except Exception as exc:
            audit.event("failed",party=LinkProxy.self_party,error=type(exc).__name__)
            raise
        audit.event("complete",party=LinkProxy.self_party)
        return result

    def _handshake(self) -> bool:
        if LinkProxy.self_party == LinkProxy.all_parties[0]:
            logging.info('BFIA协议握手角色：被动接收方')
            return self._passive_handshake()
        else:
            logging.info('BFIA协议握手角色：主动发起方')
            return self._active_handshake()

    def _active_handshake(self) -> bool:
        request = self._build_handshake_request()
        self._send_handshake_request(request)
        response = self._recv_handshake_response()
        return self._process_handshake_response(response)

    def _passive_handshake(self) -> bool:
        requests = self._recv_handshake_requests()
        response = self._process_handshake_requests(requests)
        self._send_handshake_response(response)
        return response.header.error_code == OK

    @abc.abstractmethod
    def _build_handshake_request(self) -> HandshakeRequest:
        raise NotImplementedError()

    @abc.abstractmethod
    def _build_handshake_response(self) -> HandshakeResponse:
        response = HandshakeResponse()
        response.header.error_code = OK
        return response

    @staticmethod
    def _send_handshake_request(request: HandshakeRequest):
        audit.event("request",party=LinkProxy.self_party,parameters=MessageToDict(request))
        LinkProxy.send_raw(LinkProxy.all_parties[0], request.SerializeToString())
        logging.info('BFIA协议握手请求已发送：目标=%s', LinkProxy.all_parties[0])

    @staticmethod
    def _recv_handshake_requests() -> List[HandshakeRequest]:
        requests = []
        for party in LinkProxy.all_parties:
            if party == LinkProxy.self_party:
                continue
            msg_bytes = LinkProxy.recv_raw(src_party=party)
            request = HandshakeRequest()
            request.ParseFromString(msg_bytes)
            requests.append(request)
            logging.info('BFIA协议握手请求已接收：来源=%s', party)

        return requests

    @staticmethod
    def _send_handshake_response(response: HandshakeResponse):
        audit.event("response",party=LinkProxy.self_party,parameters=MessageToDict(response))
        logging.info('BFIA协议握手响应发送：目标=%s', ','.join(LinkProxy.all_parties))
        for party in LinkProxy.all_parties:
            if party == LinkProxy.self_party:
                continue
            LinkProxy.send_raw(party, response.SerializeToString())

    @staticmethod
    def _recv_handshake_response() -> HandshakeResponse:
        msg_bytes = LinkProxy.recv_raw(LinkProxy.all_parties[0])
        response = HandshakeResponse()
        response.ParseFromString(msg_bytes)
        logging.info('BFIA协议握手响应已接收：来源=%s', LinkProxy.all_parties[0])
        return response

    def _process_handshake_requests(
        self, requests: List[HandshakeRequest]
    ) -> HandshakeResponse:
        err_code, err_msg = self._negotiate_handshake_params(requests)
        if err_code != OK:
            response = HandshakeResponse()
            response.header.error_code = err_code
            response.header.error_msg = err_msg
            return response

        return self._build_handshake_response()

    @abc.abstractmethod
    def _negotiate_handshake_params(
        self, requests: List[HandshakeRequest]
    ) -> Tuple[int, str]:
        raise NotImplementedError()

    @abc.abstractmethod
    def _process_handshake_response(self, response: HandshakeResponse) -> bool:
        return response.header.error_code == OK

    @abc.abstractmethod
    def _run_algo(self):
        raise NotImplementedError()
