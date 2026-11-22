"""Summarize bounded Phlosion GPU/CPU phase captures; all intervals are inclusive."""
import argparse
import csv
import json
import statistics
from pathlib import Path


def distribution(values):
    ordered = sorted(values)
    def percentile(fraction):
        index = (len(ordered) - 1) * fraction
        low = int(index)
        high = min(low + 1, len(ordered) - 1)
        return ordered[low] + (ordered[high] - ordered[low]) * (index - low)
    return dict(median=statistics.median(ordered), mean=statistics.mean(ordered),
                p95=percentile(.95), p99=percentile(.99), maximum=max(ordered))


def summarize(data, view=None, warmup=30, samples=200):
    if data.get("schema") != "phlosion-render-phases-v1":
        raise ValueError("Unsupported profile schema")
    if data["invalid_frames"] or data["dropped_frames"]:
        raise ValueError("Capture contains invalid or dropped frames")
    frames = sorted(data["frames"], key=lambda frame: frame["frame"])
    if len({frame["frame"] for frame in frames}) != len(frames):
        raise ValueError("Duplicate frame identities")
    if view:
        frames = [frame for frame in frames if any(span["name"] == view for span in frame["spans"])]
    frames = frames[warmup:warmup + samples]
    if not frames:
        raise ValueError("No samples remain after view selection and warmup")
    buckets = []
    for frame in frames:
        if frame["dropped_spans"]:
            raise ValueError("Phase capacity exceeded")
        entry = {}
        for index, span in enumerate(frame["spans"]):
            parent = span["parent"]
            if parent < -1 or parent >= index:
                raise ValueError("Invalid nesting")
            parent_name = "frame" if parent < 0 else frame["spans"][parent]["name"]
            key = parent_name + "/" + span["name"]
            values = entry.setdefault(key, [0., 0.])
            for i, field in enumerate(("gpu_ms", "cpu_ms")):
                if span[field] < 0:
                    raise ValueError("Negative phase duration")
                values[i] += span[field]
        buckets.append(entry)
    phases = {}
    for key in sorted(set().union(*(bucket.keys() for bucket in buckets))):
        phases[key] = dict(active_frames=sum(key in bucket for bucket in buckets),
                           gpu_ms=distribution([bucket.get(key, [0., 0.])[0] for bucket in buckets]),
                           cpu_ms=distribution([bucket.get(key, [0., 0.])[1] for bucket in buckets]))
    return dict(api=data["api"], view=view, samples=len(frames), first_frame=frames[0]["frame"],
                last_frame=frames[-1]["frame"], inclusive=True,
                gpu_ms=distribution([frame["gpu_ms"] for frame in frames]),
                cpu_frame_ms=distribution([frame["cpu_frame_ms"] for frame in frames]),
                begin_setup_wait_ms=distribution([frame["begin_wait_ms"] for frame in frames]),
                present_ms=distribution([frame["present_ms"] for frame in frames]), phases=phases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--view", choices=("field-view", "battle-view"))
    parser.add_argument("--warmup", type=int, default=30)
    parser.add_argument("--samples", type=int, default=200)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()
    if args.warmup < 0 or args.samples < 1:
        parser.error("warmup must be nonnegative and samples positive")
    report = summarize(json.loads(args.capture.read_text(encoding="utf-8-sig")),
                       args.view, args.warmup, args.samples)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.csv:
        with args.csv.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(("phase", "active_frames", "gpu_median_ms", "gpu_p95_ms", "gpu_p99_ms", "cpu_median_ms"))
            for name, phase in report["phases"].items():
                writer.writerow((name, phase["active_frames"], phase["gpu_ms"]["median"],
                                 phase["gpu_ms"]["p95"], phase["gpu_ms"]["p99"], phase["cpu_ms"]["median"]))
    print(f"{report['api']}: {report['samples']} frames, GPU median {report['gpu_ms']['median']:.3f} ms; {args.output}")


if __name__ == "__main__":
    main()
