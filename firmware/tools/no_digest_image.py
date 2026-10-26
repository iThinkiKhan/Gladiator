Import("env")

import subprocess


def rebuild_without_appended_digest(source, target, env):
    """Avoid the ESP32-S3 OTA hash-activation fault in this Arduino core.

    The ESP image's structural checksum remains enabled. ArduinoOTA also sends
    the source MD5 and Update verifies it before attempting activation.
    """
    python = env.subst("$PYTHONEXE")
    esptool = env.subst("$OBJCOPY")
    elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
    firmware = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    subprocess.run(
        [
            python,
            esptool,
            "--chip",
            "esp32s3",
            "elf2image",
            "--flash_mode",
            "qio",
            "--flash_freq",
            "80m",
            "--flash_size",
            "16MB",
            "--dont-append-digest",
            "-o",
            firmware,
            elf,
        ],
        check=True,
    )


env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.bin", rebuild_without_appended_digest
)
