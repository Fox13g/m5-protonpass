Import("env")
import os
import shutil

def build_m5launcher_bins(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    releases_dir = os.path.abspath(os.path.join(project_dir, "..", "releases"))
    os.makedirs(releases_dir, exist_ok=True)

    app_bin = os.path.join(build_dir, "firmware.bin")
    bootloader_bin = os.path.join(build_dir, "bootloader.bin")
    partitions_bin = os.path.join(build_dir, "partitions.bin")

    # 1. Cópia direta da aplicação (0x10000) para instalação rápida via M5Launcher
    launcher_app_out = os.path.join(releases_dir, "CardputerPassSentinel-M5Launcher.bin")
    if os.path.exists(app_bin):
        shutil.copyfile(app_bin, launcher_app_out)
        print(f"[M5Launcher] Gerado binário de aplicação SD: {launcher_app_out}")

    # 2. Imagem combinada (Merged 0x0000: Bootloader + Partitions + boot_app0 + App)
    # Compatível com M5Launcher (modo Full/App), M5Burner e esptool write_flash 0x0
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    boot_app0_bin = os.path.join(
        framework_dir, "tools", "partitions", "boot_app0.bin"
    ) if framework_dir else ""

    merged_out = os.path.join(releases_dir, "CardputerPassSentinel-Merged-0x0.bin")
    if os.path.exists(bootloader_bin) and os.path.exists(partitions_bin) and os.path.exists(app_bin):
        # Cria imagem contígua preenchida com 0xFF até o fim da aplicação em 0x10000+len(app)
        with open(app_bin, "rb") as f_app:
            app_data = f_app.read()

        total_size = 0x10000 + len(app_data)
        merged = bytearray(b"\xFF" * total_size)

        with open(bootloader_bin, "rb") as f_boot:
            boot_data = f_boot.read()
            merged[0x0000 : 0x0000 + len(boot_data)] = boot_data

        with open(partitions_bin, "rb") as f_part:
            part_data = f_part.read()
            merged[0x8000 : 0x8000 + len(part_data)] = part_data

        if boot_app0_bin and os.path.exists(boot_app0_bin):
            with open(boot_app0_bin, "rb") as f_b0:
                b0_data = f_b0.read()
                merged[0xE000 : 0xE000 + len(b0_data)] = b0_data

        merged[0x10000 : 0x10000 + len(app_data)] = app_data

        with open(merged_out, "wb") as f_out:
            f_out.write(merged)
        print(f"[M5Launcher/M5Burner] Gerado binário combinado (0x0): {merged_out}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", build_m5launcher_bins)

