import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('pan_analysis', Path(__file__).with_name('analyze-kestrel-pan.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PanAnalysisTests(unittest.TestCase):
    def timeline(self):
        return {'timestampFrequency': 1000,
                'publications': [{'Revision': 7, 'Timestamp': 100, 'ConsumedInputSequence': 3}],
                'renderedScenes': [{'Revision': 7, 'Timestamp': 140, 'AcceptedTimestamp': 130}]}

    def log(self, timeline):
        return 'Kestrel pan composition timeline: ' + json.dumps(timeline) + '\nKestrel pan workload validated (physical presentation remains unqualified).'

    def test_separates_queue_and_draw_without_claiming_presentation(self):
        result = module.analyze(self.log(self.timeline()))
        self.assertEqual(30, result['publicationToAcceptance']['medianMilliseconds'])
        self.assertEqual(10, result['acceptanceToDrawCallbackEnd']['medianMilliseconds'])
        self.assertEqual(40, result['publicationToDrawCallbackEnd']['medianMilliseconds'])
        self.assertFalse(result['physicalPresentationVerified'])

    def test_rejects_unvalidated_workload(self):
        with self.assertRaisesRegex(ValueError, 'not validated'):
            module.analyze('Kestrel WebGPU startup check passed (interaction qualification remains).')

    def test_rejects_inverted_acceptance_timestamp(self):
        timeline = self.timeline()
        timeline['renderedScenes'][0]['AcceptedTimestamp'] = 150
        with self.assertRaisesRegex(ValueError, 'timestamp order'):
            module.analyze(self.log(timeline))

    def test_missing_acceptance_is_unavailable_not_zero_latency(self):
        timeline = self.timeline()
        del timeline['renderedScenes'][0]['AcceptedTimestamp']
        result = module.analyze(self.log(timeline))
        self.assertEqual({'count': 0}, result['publicationToAcceptance'])
        self.assertEqual(40, result['publicationToDrawCallbackEnd']['medianMilliseconds'])

    def test_rejects_duplicate_revision(self):
        timeline = self.timeline()
        timeline['publications'] *= 2
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            module.analyze(self.log(timeline))


if __name__ == '__main__':
    unittest.main()
