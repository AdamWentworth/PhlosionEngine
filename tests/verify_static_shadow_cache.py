"""Compare complete cached/uncached fixture images, including invalidations."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

CASES = ('steady', 'move', 'remove', 'dynamic', 'scene', 'projection', 'source-off', 'masked', 'reset')

def compare(root):
    report = {}
    for api in ('d3d12', 'vulkan'):
        for case in CASES:
            stem = f'{api}-{case}'
            a, b = (Image.open(root/f'{stem}-{mode}.png').convert('RGB') for mode in ('cached', 'fresh'))
            assert a.size == b.size == (320, 200), stem
            difference = ImageChops.difference(a, b)
            peak = max(high for low, high in difference.getextrema())
            mean = sum(ImageStat.Stat(difference).mean)/3
            report[stem] = {'max_channel_255': peak, 'mean_channel_255': mean}
            # RG16 cache quantization precedes filtering; this permits at most
            # two final display steps, not misplaced/stale silhouettes.
            assert peak <= 2 and mean <= .01, (stem, report[stem])
            for mode in ('cached', 'fresh'):
                log = (root/f'{stem}-{mode}.log').read_text(encoding='utf-8-sig')
                assert f'PASS {api} {case} {mode}' in log, stem
                assert not any(x in log for x in ('VUID-', 'Validation Error', 'DEVICE_REMOVED')), stem
            refreshed = Image.open(root/f'{stem}-refresh.png').convert('RGB')
            assert refreshed.size==a.size and not ImageChops.difference(a,refreshed).getbbox(), (stem,'cache reuse changed pixels')
            log = (root/f'{stem}-refresh.log').read_text(encoding='utf-8-sig')
            assert f'PASS {api} {case} refresh' in log, stem
            assert not any(x in log for x in ('VUID-', 'Validation Error', 'DEVICE_REMOVED')), stem
            report[stem]['reuse_exact'] = True
    # Every invalidation/merge control must be visibly different from the
    # unchanged case; otherwise a dropped-light fixture could pass by accident.
    for api in ('d3d12', 'vulkan'):
        reference = Image.open(root/f'{api}-steady-fresh.png').convert('RGB')
        for case in ('move', 'remove', 'scene', 'projection', 'source-off', 'masked'):
            image = Image.open(root/f'{api}-{case}-fresh.png').convert('RGB')
            assert ImageChops.difference(reference, image).getbbox(), (api, case, 'inert control')
    return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    report = compare(args.root)
    (args.root/'image-comparison.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'PASS {len(report)} complete cached/uncached image comparisons')
