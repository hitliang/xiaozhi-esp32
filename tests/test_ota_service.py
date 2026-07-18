import hashlib
import tempfile
import unittest

from xiaozhi.ota_service import (
    ESP_APP_DESC_MAGIC,
    OtaError,
    OtaService,
    compare_versions,
    inspect_esp_image,
)


def make_image(version="2.3.0", project="xiaozhi", size=512):
    image = bytearray(size)
    image[0] = 0xE9
    image[32:36] = ESP_APP_DESC_MAGIC.to_bytes(4, "little")
    image[48:48 + len(version)] = version.encode()
    image[80:80 + len(project)] = project.encode()
    return bytes(image)


class VersionTests(unittest.TestCase):
    def test_numeric_version_comparison(self):
        self.assertEqual(compare_versions("2.3", "2.3.0"), 0)
        self.assertLess(compare_versions("2.2.9", "2.3.0"), 0)
        self.assertGreater(compare_versions("2.10.0", "2.9.9"), 0)

    def test_bad_version_is_rejected(self):
        with self.assertRaises(OtaError):
            compare_versions("2.3-beta", "2.3.0")


class ImageTests(unittest.TestCase):
    def test_reads_app_descriptor(self):
        info = inspect_esp_image(make_image())
        self.assertEqual(info.project, "xiaozhi")
        self.assertEqual(info.version, "2.3.0")

    def test_rejects_wrong_magic(self):
        with self.assertRaises(OtaError):
            inspect_esp_image(bytes(512))


class OtaServiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.service = OtaService(
            self.temp.name,
            "http://example.test",
            "ws://example.test/ws",
            "secret-token",
        )

    def tearDown(self):
        self.temp.cleanup()

    def test_publish_and_check(self):
        image = make_image()
        manifest = self.service.publish(
            image,
            version="2.3.0",
            board="esp32-s3-touch-amoled-1.8",
            release_notes="Adaptive ear training",
        )
        self.assertEqual(manifest.sha256, hashlib.sha256(image).hexdigest())
        response = self.service.check(
            device_id="device-a",
            current_version="2.2.6",
            board="esp32-s3-touch-amoled-1.8",
        )
        self.assertEqual(response["firmware"]["version"], "2.3.0")
        self.assertEqual(response["firmware"]["size"], len(image))
        self.assertIn("server_time", response)
        self.assertIn("websocket", response)

    def test_latest_device_gets_no_firmware(self):
        self.service.publish(
            make_image(), version="2.3.0", board="esp32-s3-touch-amoled-1.8"
        )
        response = self.service.check(
            device_id="device-a",
            current_version="2.3.0",
            board="esp32-s3-touch-amoled-1.8",
        )
        self.assertNotIn("firmware", response)

    def test_force_does_not_reinstall_same_version_forever(self):
        self.service.publish(
            make_image(),
            version="2.3.0",
            board="esp32-s3-touch-amoled-1.8",
            force=True,
        )
        response = self.service.check(
            device_id="device-a",
            current_version="2.3.0",
            board="esp32-s3-touch-amoled-1.8",
        )
        self.assertNotIn("firmware", response)

    def test_wrong_board_gets_no_firmware(self):
        self.service.publish(
            make_image(), version="2.3.0", board="esp32-s3-touch-amoled-1.8"
        )
        response = self.service.check(
            device_id="device-a", current_version="2.2.6", board="other-board"
        )
        self.assertNotIn("firmware", response)

    def test_version_and_project_must_match(self):
        with self.assertRaises(OtaError):
            self.service.publish(
                make_image(version="2.3.1"),
                version="2.3.0",
                board="esp32-s3-touch-amoled-1.8",
            )
        with self.assertRaises(OtaError):
            self.service.publish(
                make_image(project="other"),
                version="2.3.0",
                board="esp32-s3-touch-amoled-1.8",
            )

    def test_token_comparison(self):
        self.assertTrue(self.service.is_authorized("secret-token"))
        self.assertFalse(self.service.is_authorized("wrong"))


if __name__ == "__main__":
    unittest.main()
