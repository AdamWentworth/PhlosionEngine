"""Check the native emissive-sign reflection oracle after PhlosionPlanarReflection CTests."""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def project(point):
    eye = np.array([0., 2., 6.])
    forward = np.array([0., -2., -7.]); forward /= np.linalg.norm(forward)
    right = np.array([1., 0., 0.]); up = np.cross(right, forward)
    delta = np.asarray(point) - eye
    focal = 320 / (2 * np.tan(np.pi / 6))
    return np.array([256 + focal * (delta @ right) / (delta @ forward),
                     160 - focal * (delta @ up) / (delta @ forward)])


def patch(image, point):
    x, y = np.rint(project(point)).astype(int)
    return image[y-1:y+2, x-1:x+2, 0].mean()


def edge_width(image):
    x, y = np.rint(project([-1., -1.5, -1.])).astype(int)
    row = image[y-2:y+3, :, 0].mean(axis=0)
    plateau = np.median(row[x+7:x+22])
    values = row[x-6:x+7] / plateau
    coordinates = np.arange(x-6, x+7, dtype=float)
    # First crossing of each level; interpolation avoids rounding away subpixels.
    def crossing(level):
        at = np.flatnonzero(values >= level)[0]
        assert at > 0
        return coordinates[at-1] + (level-values[at-1])/(values[at]-values[at-1])
    return float(crossing(.9)-crossing(.1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--apis', nargs='+', choices=('d3d12', 'vulkan', 'opengl'),
                        default=['d3d12', 'vulkan'] if sys.platform == 'win32' else ['vulkan'])
    args = parser.parse_args()
    if len(args.apis) != len(set(args.apis)):
        parser.error('--apis must not contain duplicates')
    report, images = {}, {}
    for api in args.apis:
        images[api] = {mode: np.asarray(Image.open(args.captures/f'{api}-{mode}.png').convert('RGB'), dtype=float)
                       for mode in ('emissive', 'emissive-rough', 'emissive-off')}
        clear, rough, off = (images[api][mode] for mode in ('emissive', 'emissive-rough', 'emissive-off'))
        assert clear.shape == rough.shape == off.shape == (320, 512, 3)
        assert off.max() == 0, (api, 'emission-off without illumination must be black')
        for name, image in (('clear', clear), ('rough', rough)):
            stroke = patch(image, [.3, -1.5, -1.])
            gap = patch(image, [.3, -1.82, -1.])
            assert stroke > 10 and gap < stroke*.2, (api, name, 'lost reflected emissive texture', stroke, gap)
        widths = [edge_width(image) for image in (clear, rough)]
        assert widths[1] > widths[0]+.3, (api, 'rough water does not soften reflection edge', widths)
        # Main sign is unchanged; only the receiver roughness differs.
        assert np.array_equal(clear[:160], rough[:160]), (api, 'receiver changed main sign')
        report[api] = {'clear_edge_10_90_px': widths[0], 'rough_edge_10_90_px': widths[1]}
    reference = args.apis[0]
    for api in args.apis[1:]:
        for mode in images[api]:
            mae = float(np.abs(images[api][mode]-images[reference][mode]).mean())
            assert mae < 1, (api, mode, 'native reflection mismatch', mae)
            report[api][mode+'_mae_255'] = mae
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print('PASS emissive texture, source-off, roughness edge spread, unchanged sign and API parity:', ', '.join(args.apis))


if __name__ == '__main__':
    main()
