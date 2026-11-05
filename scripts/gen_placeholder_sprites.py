#!/usr/bin/env python3
"""Generate the placeholder hero source frames used by the atlas pipeline."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

WIDTH = 48
HEIGHT = 64
BASELINE = 59
STATES = (
    ("idle", 4),
    ("run", 4),
    ("jump", 4),
    ("fall", 4),
    ("shoot", 4),
    ("hurt", 4),
    ("death", 4),
)
HEROES = {
    "scout": {
        "shoulder": 5,
        "hip": 4,
        "head": 4,
        "leg": 2,
        "weapon": "rifle",
    },
    "gunner": {
        "shoulder": 8,
        "hip": 7,
        "head": 5,
        "leg": 4,
        "weapon": "minigun",
    },
    "assault": {
        "shoulder": 7,
        "hip": 5,
        "head": 4,
        "leg": 3,
        "weapon": "rocket",
    },
}

BODY = (224, 224, 224, 255)
LIGHT = (250, 250, 250, 255)
MID = (178, 178, 178, 255)
DARK = (112, 112, 112, 255)
OUTLINE = (54, 54, 54, 255)
SHADOW = (24, 24, 24, 52)
FLASH = (255, 255, 255, 255)


def line(draw: ImageDraw.ImageDraw, points, fill, width):
    draw.line(points, fill=fill, width=width)


def draw_shadow(draw: ImageDraw.ImageDraw, x: float, y: float, width: float):
    draw.ellipse(
        (x - width, y - 2, x + width, y + 2),
        fill=SHADOW,
    )


def draw_weapon(
    draw: ImageDraw.ImageDraw,
    hero: dict,
    cx: float,
    y: float,
    state: str,
    frame: int,
    facing_right: bool,
):
    weapon = hero["weapon"]
    direction = 1 if facing_right else -1
    muzzle = cx + 18 * direction
    if weapon == "rifle":
        line(draw, [(cx + 2 * direction, y + 2), (cx + 15 * direction, y)], LIGHT, 2)
        draw.rectangle(
            (min(cx + 10 * direction, cx + 16 * direction), y - 2,
             max(cx + 10 * direction, cx + 16 * direction), y + 2),
            fill=DARK,
            outline=OUTLINE,
        )
        draw.rectangle(
            (min(cx + 15 * direction, cx + 21 * direction), y - 1,
             max(cx + 15 * direction, cx + 21 * direction), y + 1),
            fill=MID,
            outline=OUTLINE,
        )
        muzzle = cx + 21 * direction
    elif weapon == "minigun":
        draw.rectangle(
            (min(cx + 2 * direction, cx + 21 * direction), y - 4,
             max(cx + 2 * direction, cx + 21 * direction), y + 4),
            fill=MID,
            outline=OUTLINE,
        )
        for offset in (-3, 0, 3):
            line(
                draw,
                [(cx + 4 * direction, y + offset), (cx + 22 * direction, y + offset)],
                LIGHT,
                1,
            )
        muzzle = cx + 22 * direction
    else:
        draw.rectangle(
            (min(cx + 1 * direction, cx + 21 * direction), y - 5,
             max(cx + 1 * direction, cx + 21 * direction), y + 5),
            fill=MID,
            outline=OUTLINE,
        )
        draw.rectangle(
            (min(cx + 20 * direction, cx + 23 * direction), y - 4,
             max(cx + 20 * direction, cx + 23 * direction), y + 4),
            fill=DARK,
            outline=OUTLINE,
        )
        muzzle = cx + 23 * direction

    if state == "shoot" and frame in (1, 2):
        length = 5 if frame == 1 else 3
        line(
            draw,
            [(muzzle, y), (muzzle + length * direction, y)],
            FLASH,
            2,
        )
        line(
            draw,
            [(muzzle, y), (muzzle + 2 * direction, y - 3)],
            FLASH,
            1,
        )
        line(
            draw,
            [(muzzle, y), (muzzle + 2 * direction, y + 3)],
            FLASH,
            1,
        )


def draw_death(
    draw: ImageDraw.ImageDraw,
    hero: dict,
    cx: float,
    frame: int,
):
    head = hero["head"]
    leg = hero["leg"]
    draw_shadow(draw, cx, BASELINE, 18)
    if frame == 0:
        draw.ellipse(
            (cx - head, 17, cx + head, 17 + head * 2),
            fill=BODY,
            outline=OUTLINE,
        )
        line(draw, [(cx, 25), (cx + 2, 43)], BODY, 7)
        line(draw, [(cx - 4, 42), (cx - 6, 56)], BODY, leg + 1)
        line(draw, [(cx + 4, 43), (cx + 10, 55)], BODY, leg + 1)
        line(draw, [(cx - 2, 28), (cx - 9, 36)], BODY, 2)
    elif frame == 1:
        draw.ellipse(
            (cx + 2 - head, 29, cx + 2 + head, 29 + head * 2),
            fill=BODY,
            outline=OUTLINE,
        )
        line(draw, [(cx, 34), (cx - 6, 46)], BODY, 7)
        line(draw, [(cx - 5, 46), (cx - 11, 55)], BODY, leg + 1)
        line(draw, [(cx - 1, 45), (cx + 3, 57)], BODY, leg + 1)
        line(draw, [(cx - 1, 38), (cx + 8, 43)], BODY, 2)
    elif frame == 2:
        draw.ellipse(
            (cx + 6 - head, 43, cx + 6 + head, 43 + head * 2),
            fill=BODY,
            outline=OUTLINE,
        )
        line(draw, [(cx + 2, 47), (cx - 7, 51)], BODY, 7)
        line(draw, [(cx - 6, 51), (cx - 15, 54)], BODY, leg + 1)
        line(draw, [(cx - 7, 53), (cx - 15, 58)], BODY, leg + 1)
        line(draw, [(cx + 1, 48), (cx + 7, 54)], BODY, 2)
        line(draw, [(cx - 2, 55), (cx + 5, 58)], DARK, 1)
    else:
        draw.ellipse(
            (cx + 2 - head, 48, cx + 2 + head, 48 + head * 2),
            fill=BODY,
            outline=OUTLINE,
        )
        line(draw, [(cx - 3, 53), (cx - 13, 55)], BODY, 7)
        line(draw, [(cx - 13, 55), (cx - 20, 57)], BODY, leg + 1)
        line(draw, [(cx - 12, 57), (cx - 19, 60)], BODY, leg + 1)
        line(draw, [(cx - 4, 54), (cx + 5, 58)], DARK, 1)
        draw.rectangle((cx - 18, 58, cx - 10, 60), fill=DARK, outline=OUTLINE)


def draw_hero(hero_name: str, state: str, frame: int) -> Image.Image:
    hero = HEROES[hero_name]
    image = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    if state == "death":
        draw_death(draw, hero, 24, frame)
        return image

    phase = (frame / 4.0) * math.tau
    bob = 0
    lean = 0
    stride = 0
    if state == "idle":
        bob = int(round(math.sin(phase)))
    elif state == "run":
        bob = int(round(math.sin(phase * 2.0)))
        lean = 1
        stride = int(round(math.sin(phase) * 7.0))
    elif state == "jump":
        bob = -3
        lean = -1
    elif state == "fall":
        bob = -2
        lean = 1
    elif state == "shoot":
        bob = int(round(math.sin(phase) * 0.5))
        lean = -1
    elif state == "hurt":
        bob = 1
        lean = -2

    cx = 24 + lean
    head = hero["head"]
    shoulder = hero["shoulder"]
    hip = hero["hip"]
    head_y = 12 + bob
    shoulder_y = 24 + bob
    hip_y = 44 + bob

    draw_shadow(draw, 24, BASELINE, 13)

    if state == "jump":
        left_foot = (cx - 7, 53)
        right_foot = (cx + 5, 54)
        left_knee = (cx - 6, 49)
        right_knee = (cx + 4, 49)
    elif state == "fall":
        left_foot = (cx - 8, 61)
        right_foot = (cx + 8, 60)
        left_knee = (cx - 6, 52)
        right_knee = (cx + 6, 52)
    elif state == "run":
        left_foot = (cx - 4 + stride, 59)
        right_foot = (cx + 4 - stride, 59)
        left_knee = (cx - 3 + stride // 2, 53)
        right_knee = (cx + 3 - stride // 2, 53)
    elif state == "hurt":
        left_foot = (cx - 8, 58)
        right_foot = (cx + 10, 57)
        left_knee = (cx - 5, 53)
        right_knee = (cx + 6, 52)
    else:
        left_foot = (cx - 5, 59)
        right_foot = (cx + 5, 59)
        left_knee = (cx - 3, 53)
        right_knee = (cx + 3, 53)

    leg_width = hero["leg"] + 1
    line(draw, [(cx - hip // 2, hip_y), left_knee, left_foot], BODY, leg_width)
    line(draw, [(cx + hip // 2, hip_y), right_knee, right_foot], BODY, leg_width)
    draw.ellipse(
        (left_foot[0] - 2, left_foot[1] - 2, left_foot[0] + 2, left_foot[1] + 1),
        fill=MID,
        outline=OUTLINE,
    )
    draw.ellipse(
        (right_foot[0] - 2, right_foot[1] - 2, right_foot[0] + 2, right_foot[1] + 1),
        fill=MID,
        outline=OUTLINE,
    )

    if state == "jump":
        left_hand = (cx - 10, 18)
        right_hand = (cx + 8, 30)
    elif state == "fall":
        left_hand = (cx - 11, 25)
        right_hand = (cx + 11, 24)
    elif state == "shoot":
        left_hand = (cx + 9, 30)
        right_hand = (cx + 5, 33)
    elif state == "hurt":
        left_hand = (cx - 9, 20)
        right_hand = (cx + 8, 18)
    elif state == "run":
        left_hand = (cx - 7, 35)
        right_hand = (cx + 7, 33)
    else:
        left_hand = (cx - 7, 37)
        right_hand = (cx + 7, 37)

    line(
        draw,
        [(cx - shoulder + 2, shoulder_y + 3), left_hand],
        BODY,
        2,
    )
    line(
        draw,
        [(cx + shoulder - 2, shoulder_y + 3), right_hand],
        BODY,
        2,
    )

    torso = [
        (cx - shoulder, shoulder_y),
        (cx + shoulder, shoulder_y),
        (cx + hip, hip_y),
        (cx - hip, hip_y),
    ]
    draw.polygon(torso, fill=BODY, outline=OUTLINE)
    line(draw, [(cx - hip + 1, hip_y - 1), (cx + hip - 1, hip_y - 1)], DARK, 1)

    draw.ellipse(
        (cx - head, head_y, cx + head, head_y + head * 2),
        fill=LIGHT,
        outline=OUTLINE,
    )
    cap_y = head_y - 1
    draw.rectangle(
        (cx - head, cap_y, cx + head, cap_y + 2),
        fill=DARK,
        outline=OUTLINE,
    )
    eye_x = cx + head // 2
    draw.rectangle(
        (eye_x, head_y + head, eye_x + 2, head_y + head + 1),
        fill=OUTLINE,
    )

    weapon_y = 31 + bob
    if state == "shoot":
        weapon_y = 30 + bob
    draw_weapon(draw, hero, cx, weapon_y, state, frame, True)

    if state == "hurt":
        line(draw, [(cx - 12, 12), (cx - 15, 8)], LIGHT, 1)
        line(draw, [(cx + 12, 11), (cx + 15, 7)], LIGHT, 1)
    return image


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    output = root / "assets" / "sprites" / "source"
    output.mkdir(parents=True, exist_ok=True)
    for old in output.glob("*.png"):
        old.unlink()

    count = 0
    for hero_name in HEROES:
        for state, frame_count in STATES:
            for frame in range(frame_count):
                image = draw_hero(hero_name, state, frame)
                image.save(output / f"{hero_name}_{state}_{frame}.png")
                count += 1
    print(f"wrote {count} source frames to {output}")


if __name__ == "__main__":
    main()
