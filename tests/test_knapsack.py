#!/usr/bin/env python3
import itertools
import random
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def compile_source(source: Path, binary: Path, include_src: bool) -> None:
    command = ["g++", "-std=gnu++23", "-O2", str(source), "-o", str(binary)]
    if include_src:
        command.extend(["-I", str(ROOT / "src")])
    subprocess.run(command, check=True, cwd=ROOT)


def solve(binary: Path, text: str) -> tuple[int, list[int], str]:
    completed = subprocess.run(
        [str(binary)], input=text, text=True, capture_output=True, check=True
    )
    lines = completed.stdout.splitlines()
    score = int(lines[0].split(":", 1)[1])
    selected = [int(value) - 1 for value in lines[1].split(":", 1)[1].split()]
    return score, selected, completed.stdout


def brute_force(profit: list[int], consumption: list[list[int]], capacity: list[int]) -> int:
    best = 0
    for bits in itertools.product((0, 1), repeat=len(profit)):
        used = [0] * len(capacity)
        value = 0
        for item, take in enumerate(bits):
            if not take:
                continue
            value += profit[item]
            for resource in range(len(capacity)):
                used[resource] += consumption[item][resource]
        if all(used[i] <= capacity[i] for i in range(len(capacity))):
            best = max(best, value)
    return best


def make_input(profit: list[int], consumption: list[list[int]], capacity: list[int]) -> str:
    lines = [f"{len(profit)} {len(capacity)}", " ".join(map(str, capacity))]
    for value, usage in zip(profit, consumption):
        lines.append(" ".join(map(str, [value, *usage])))
    return "\n".join(lines) + "\n"


def main() -> None:
    with tempfile.TemporaryDirectory() as temp:
        temp_dir = Path(temp)
        split_knapsack = temp_dir / "knapsack_split"
        combined_knapsack = temp_dir / "knapsack_combined"
        split_intro = temp_dir / "intro_split"
        combined_intro = temp_dir / "intro_combined"

        compile_source(
            ROOT / "src/01_multidimensional_knapsack.cpp", split_knapsack, True
        )
        compile_source(ROOT / "src/02_intro_heuristics.cpp", split_intro, True)

        subprocess.run(
            [str(ROOT / "combiner.sh"), "src/01_multidimensional_knapsack.cpp"],
            check=True,
            cwd=ROOT,
        )
        subprocess.run(
            [str(ROOT / "combiner.sh"), "src/02_intro_heuristics.cpp"],
            check=True,
            cwd=ROOT,
        )
        compile_source(
            ROOT / "combined/combined_01_multidimensional_knapsack.cpp",
            combined_knapsack,
            False,
        )
        compile_source(
            ROOT / "combined/combined_02_intro_heuristics.cpp", combined_intro, False
        )

        sample = (ROOT / "examples/knapsack_small.txt").read_text()
        split_score, split_selected, split_output = solve(split_knapsack, sample)
        combined_score, combined_selected, combined_output = solve(combined_knapsack, sample)
        assert split_score == 20
        assert split_selected == [2, 3]
        assert (split_score, split_selected, split_output) == (
            combined_score,
            combined_selected,
            combined_output,
        )

        rng = random.Random(0)
        for _ in range(100):
            item_count = 8
            resource_count = rng.randint(2, 3)
            capacity = [rng.randint(8, 20) for _ in range(resource_count)]
            profit = [rng.randint(1, 30) for _ in range(item_count)]
            consumption = [
                [rng.randint(1, capacity[r]) for r in range(resource_count)]
                for _ in range(item_count)
            ]
            text = make_input(profit, consumption, capacity)
            actual, selected, _ = solve(split_knapsack, text)
            expected = brute_force(profit, consumption, capacity)
            assert actual == expected

            used = [0] * resource_count
            selected_score = 0
            for item in selected:
                selected_score += profit[item]
                for resource in range(resource_count):
                    used[resource] += consumption[item][resource]
            assert selected_score == actual
            assert all(used[r] <= capacity[r] for r in range(resource_count))

    print("lagbeam sample tests passed")


if __name__ == "__main__":
    main()
