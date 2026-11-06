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
import sys

import secretflow.distributed as sfd
from secretflow.distributed.primitive import DISTRIBUTION_MODE
from secretflow.ic.proxy import LinkProxy
from secretflow.utils.logging import LOG_FORMAT, get_logging_level, set_logging_level


def _init_log(self_party: str, logging_level: str):
    set_logging_level(logging_level)
    logging.basicConfig(
        level=get_logging_level(),
        format=LOG_FORMAT,
        handlers=[
            logging.FileHandler(self_party + '.log', mode='w', encoding='utf-8'),
            logging.StreamHandler(sys.stdout),
        ],
        force=True,
    )


def _init_link(link_config: dict):
    self_party = link_config['self_party']
    parties = link_config['parties']

    for party in parties.values():
        assert (
            'address' in party
        ), f'There is no address for party {party} in link config.'

    addresses = {}
    for party, addr in parties.items():
        if party == self_party:
            addresses[party] = addr.get('listen_addr', addr['address'])
        else:
            addresses[party] = addr['address']

    peer_addresses = ', '.join(
        f'{party}={address}'
        for party, address in addresses.items()
        if party != self_party
    )
    advertised_addresses = {
        party: addr['address'] for party, addr in parties.items()
    }
    logging.info(
        'BFIA协议地址配置：本方=%s（监听=%s，对外=%s），对端=%s',
        self_party,
        addresses[self_party],
        advertised_addresses[self_party],
        peer_addresses or '无',
    )
    LinkProxy.init(
        addresses=addresses,
        self_party=self_party,
        advertised_addresses=advertised_addresses,
    )


def _stop_link():
    LinkProxy.stop()


def _get_ic_handler(config: dict, dataset: dict):
    if config['algo'] == 'sgb':
        from secretflow.ic.handler import SgbIcHandler

        return SgbIcHandler(config, dataset)
    elif config['algo'] in ('he_woe', 'he-woe'):
        from secretflow.ic.handler import HeWoeIcHandler

        return HeWoeIcHandler(config, dataset)
    else:
        raise NotImplementedError(f'Unsupported algorithm {config["algo"]}')


def run(config: dict, dataset: dict, logging_level: str = 'info'):
    """Initialize runtime environment and run the specified algorithm

    Args:
        config : The configuration for network communication and
            specified algorithm
        dataset : The input dataset for training the model
        logging_level : Optional; The logging level, could be `debug`,
            `info`, `warning`, `error`, `critical`, not case-sensitive.
    """

    sfd.set_distribution_mode(mode=DISTRIBUTION_MODE.INTERCONNECTION)

    _init_log(config['link']['self_party'], logging_level)
    logging.info(
        'BFIA协议运行开始：算法=%s，本方=%s，参与方=%s，日志级别=%s',
        config.get('algo', 'unknown'),
        config['link']['self_party'],
        ','.join(config['link']['parties'].keys()),
        logging_level,
    )

    _init_link(config['link'])

    try:
        handler = _get_ic_handler(config, dataset)
        result = handler.run()
        logging.info(
            'BFIA协议算法阶段返回：算法=%s，本方=%s，返回类型=%s（SGB等过程型算法以进程状态判定）',
            config.get('algo', 'unknown'),
            config['link']['self_party'],
            type(result).__name__ if result is not None else 'None',
        )
        return result
    finally:
        stats = LinkProxy.stats()
        logging.info(
            'BFIA协议通信统计：本方=%s，实际发送=%d条，实际接收=%d条',
            config['link']['self_party'],
            stats['sent'],
            stats['received'],
        )
        _stop_link()
