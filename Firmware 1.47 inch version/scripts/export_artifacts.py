from pathlib import Path
from shutil import copy2
import subprocess

Import("env")

DIST_HEX_FILES = {"app.hex", "bootloader.hex", "firmware_combined.hex"}

if env.subst("$PIOENV") == "bootloader":
    env.Append(
        LINKFLAGS=[
            "-nostdlib",
            "-nostartfiles",
            "-nodefaultlibs",
            "-Wl,--gc-sections",
        ]
    )


def write_merged_hex(boot_hex, app_hex, output_hex):
    if not boot_hex.exists() or not app_hex.exists():
        return

    lines = []
    for src in (boot_hex, app_hex):
        with src.open("r", encoding="ascii") as f:
            for line in f:
                record_type = line[7:9] if len(line) >= 9 else ""
                if line.strip() != ":00000001FF" and record_type != "05":
                    lines.append(line)
    lines.append(":00000001FF\n")
    output_hex.write_text("".join(lines), encoding="ascii")


def write_combined_hex(dist_dir):
    app_hex = dist_dir / "app.hex"
    combined = dist_dir / "firmware_combined.hex"
    if app_hex.exists():
        copy2(app_hex, combined)


def artifact_stem(env_name):
    if env_name == "app":
        return "app"
    if env_name == "bootloader":
        return "bootloader"
    return None


def clean_dist_dir(dist_dir):
    for path in dist_dir.iterdir():
        if path.name in DIST_HEX_FILES:
            continue
        if path.is_dir():
            import shutil
            shutil.rmtree(path)
        else:
            path.unlink()


def export_elf_artifacts(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    project_dir = Path(env.subst("$PROJECT_DIR"))
    env_name = env.subst("$PIOENV")
    dist_dir = project_dir / "dist"
    dist_dir.mkdir(exist_ok=True)
    clean_dist_dir(dist_dir)

    stem = artifact_stem(env_name)
    if stem is None:
        return

    elf = build_dir / "firmware.elf"
    hex_file = build_dir / "firmware.hex"
    objcopy = env.subst("$OBJCOPY")

    if elf.exists():
        subprocess.check_call([objcopy, "-O", "ihex", str(elf), str(hex_file)])

    src = build_dir / "firmware.hex"
    if src.exists():
        dst = dist_dir / f"{stem}.hex"
        if env_name == "app" and (dist_dir / "bootloader.hex").exists():
            write_merged_hex(dist_dir / "bootloader.hex", src, dst)
        else:
            copy2(src, dst)

    write_combined_hex(dist_dir)


def export_bin_artifact(source, target, env):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    dist_dir = project_dir / "dist"
    dist_dir.mkdir(exist_ok=True)
    clean_dist_dir(dist_dir)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", export_elf_artifacts)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_bin_artifact)
