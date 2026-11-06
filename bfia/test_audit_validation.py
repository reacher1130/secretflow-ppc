import json
import tempfile
import unittest
from pathlib import Path
import numpy as np
from validate_numerics import reconstruct
from collect_protocol_audit import inspect_pair

class ValidationTests(unittest.TestCase):
    def test_modular_reconstruction_preserves_small_values(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths=[];mod=1<<64;scale=1<<18
            targets=[int(1.25*scale),int(-2.5*scale)%mod]
            share_a=[(1<<63)+123456789,(1<<62)+987654321]
            share_b=[(v-a)%mod for v,a in zip(targets,share_a)]
            for rank,values in enumerate((share_a,share_b)):
                p=Path(tmp)/str(rank);p.write_text(json.dumps({'format':'bfia-ring-share-v1','rank':rank,'ring_bits':64,'fraction_bits':18,'shape':[2,1],'shares_hex':[format(x,'016x') for x in values]}));paths.append(p)
            np.testing.assert_array_equal(reconstruct(paths),[[1.25],[-2.5]])
            broken=json.loads(paths[1].read_text());broken['rank']=0;paths[1].write_text(json.dumps(broken))
            with self.assertRaises(AssertionError):reconstruct(paths)

    def test_missing_handshake_cannot_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for role in ('alice','bob'):
                (root/(role+'.log')).write_text('BFIA_EVENT '+json.dumps({'stage':'compute','event':'complete','seq':1,'timestamp_ms':0})+'\n')
            result=inspect_pair(root,'cpp')
            self.assertFalse(result['pass'])
            self.assertFalse(result['checks']['alice_handshake'])

class HandshakeTests(unittest.TestCase):
    def test_refusal_never_runs_algorithm(self):
        from secretflow.ic.handler.handler import IcHandler
        from secretflow.ic.proxy import LinkProxy
        class Refused(IcHandler):
            called=False
            def _handshake(self):return False
            def _build_handshake_request(self):raise AssertionError()
            def _build_handshake_response(self):raise AssertionError()
            def _negotiate_handshake_params(self,requests):raise AssertionError()
            def _process_handshake_response(self,response):return False
            def _run_algo(self):self.called=True
        LinkProxy.self_party='alice';LinkProxy.all_parties=['alice','bob']
        instance=Refused(None)
        with self.assertRaisesRegex(RuntimeError,'handshake refused'):instance.run()
        self.assertFalse(instance.called)

if __name__=='__main__':unittest.main()
