"""One device-bound update operation; downloads do not hold the BLE exclusive lock."""

import asyncio
from quotaframe_bridge.service.ota import OtaService, UpgradeError
from quotaframe_bridge.sources.firmware_release import FirmwareReleaseSource


async def install_firmware(graph, device, manifest_url, confirm, progress, *, source=None):
    """Bind this invocation to a session, connection and explicitly confirmed image."""
    task = asyncio.current_task()
    if device.update_task is not None:
        raise UpgradeError("firmware update is already running")
    device.update_task = task
    device.update_cancellable = True
    device.update_cancel_requested = False
    manager = device.manager
    source = FirmwareReleaseSource() if source is None else source

    def require_current(connection):
        if (graph.get(device.address) is not device or device.update_task is not task
                or manager.current_transport is not connection or not manager.connected):
            raise UpgradeError("device connection changed; start a new update")

    try:
        connection = manager.current_transport
        require_current(connection)
        initial = await manager.query_status()
        require_current(connection)
        async with asyncio.timeout(60):
            manifest = await source.fetch_manifest(manifest_url)
            image = manifest.for_target(initial.target, initial.firmware_project)
            OtaService._require_compatible(initial, image)
            payload = await source.download_image(image)
        OtaService._validate_payload(image, payload)
        require_current(connection)
        if not await confirm(device, initial, image):
            return None
        require_current(connection)

        def report(value):
            if graph.get(device.address) is device and device.update_task is task:
                # Disable cancellation before file closure and the char_end commit.
                if value[0] >= value[1]:
                    device.update_cancellable = False
                progress(value)

        async def execute(transport):
            require_current(connection)
            return await OtaService().upgrade(transport, image, payload, report, expected_status=initial)

        return await device.session.run_exclusive(execute)
    finally:
        if device.update_task is task:
            device.update_task = None
            device.update_cancellable = False
