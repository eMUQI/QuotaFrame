import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test from "node:test";
import { flashSession } from "../src/flash-session.js";

const FIRMWARE = Uint8Array.from([1, 2, 3, 4, 5]);
const DIGEST = createHash("sha256").update(FIRMWARE).digest("hex");

function fixture() {
  const port = { readable: {}, writable: {} };
  const manifest = {
    builds: [
      {
        chipFamily: "ESP32-S3",
        parts: [
          { path: "../firmware/full.bin", offset: 0, sha256: DIGEST },
        ],
      },
    ],
  };
  const downloaded = [];
  const revoked = [];
  let objectUrls = 0;
  let disconnects = 0;
  let payload = FIRMWARE;
  const environment = {
    fetch: async (url) => {
      if (url === "https://example.test/manifests/board.json")
        return { ok: true, json: async () => manifest };
      downloaded.push(url);
      return { ok: true, arrayBuffer: async () => payload.buffer.slice(0) };
    },
    crypto: globalThis.crypto,
    Blob: class {
      constructor(parts) {
        this.parts = parts;
      }
    },
    URL: {
      createObjectURL: () => `blob:image-${++objectUrls}`,
      revokeObjectURL: (url) => revoked.push(url),
    },
    esploader: {
      transport: {
        device: port,
        disconnect: async () => {
          disconnects++;
          port.readable = null;
          port.writable = null;
        },
      },
    },
  };
  const options = {
    port,
    manifestUrl: "https://example.test/manifests/board.json",
    eraseFirst: false,
    onEvent: () => {},
  };
  return {
    environment,
    options,
    manifest,
    downloaded,
    revoked,
    corrupt: () => {
      payload = Uint8Array.from([9, 9, 9]);
    },
    disconnects: () => disconnects,
  };
}

test("manifest failures cannot invoke the flashing engine", async () => {
  for (const fetch of [
    async () => {
      throw new Error("offline");
    },
    async () => ({ ok: false }),
    async () => ({ ok: true, json: async () => ({ builds: [] }) }),
    // An image the assembler did not record a digest for is not installable:
    // accepting it would silently restore unverified writes.
    async () => ({
      ok: true,
      json: async () => ({
        builds: [
          { chipFamily: "ESP32-S3", parts: [{ path: "a.bin", offset: 0 }] },
        ],
      }),
    }),
    async () => ({
      ok: true,
      json: async () => ({
        builds: [
          {
            chipFamily: "ESP32-S3",
            parts: [{ path: "a.bin", offset: 0, sha256: "not-a-digest" }],
          },
        ],
      }),
    }),
  ]) {
    const f = fixture();
    f.environment.fetch = fetch;
    await assert.rejects(
      flashSession(
        () => assert.fail("engine should not run"),
        f.options,
        f.environment,
      ),
      { code: "fetch_manifest_failed" },
    );
    assert.equal(f.disconnects(), 0);
  }
});

test("a firmware image that fails its digest is never written", async () => {
  const f = fixture();
  f.corrupt();
  await assert.rejects(
    flashSession(
      () => assert.fail("engine should not run"),
      f.options,
      f.environment,
    ),
    { code: "firmware_checksum_failed" },
  );
  assert.equal(f.disconnects(), 0);
  assert.deepEqual(f.revoked, []);
});

test("an unreachable firmware image stops the install before the engine runs", async () => {
  const f = fixture();
  f.environment.fetch = async (url) =>
    url.endsWith("board.json")
      ? { ok: true, json: async () => f.manifest }
      : { ok: false, status: 404 };
  await assert.rejects(
    flashSession(
      () => assert.fail("engine should not run"),
      f.options,
      f.environment,
    ),
    { code: "failed_firmware_download" },
  );
});

test("engine receives verified images and the explicit erase choice", async () => {
  const f = fixture();
  await flashSession(
    async (notify, port, url, manifest, erase) => {
      assert.equal(port, f.options.port);
      assert.equal(url, f.options.manifestUrl);
      assert.equal(erase, false);
      // The engine resolves part paths itself, so it must be handed the
      // verified bytes rather than the original URL it would re-download.
      const part = manifest.builds[0].parts[0];
      assert.match(part.path, /^blob:/);
      assert.equal(part.offset, 0);
      assert.equal(part.sha256, DIGEST);
      assert.equal(manifest.builds[0].chipFamily, "ESP32-S3");
      notify({ state: "finished" });
    },
    f.options,
    f.environment,
  );
  assert.deepEqual(f.downloaded, ["https://example.test/firmware/full.bin"]);
  assert.deepEqual(f.revoked, ["blob:image-1"]);
  assert.equal(f.disconnects(), 1);
  assert.equal(f.environment.esploader, undefined);
});

test("reports the download phase before the engine takes over", async () => {
  const f = fixture();
  const states = [];
  f.options.onEvent = (event) => states.push(event.state);
  await flashSession(
    async (notify) => notify({ state: "finished" }),
    f.options,
    f.environment,
  );
  assert.deepEqual(states, ["preparing", "finished"]);
});

test("preserves the reported failure if a subsequent reset throws, and releases the port", async () => {
  const f = fixture();
  await assert.rejects(
    flashSession(
      async (notify) => {
        notify({
          state: "error",
          message: "write failed",
          details: { error: "write_failed" },
        });
        throw new Error("reset failed");
      },
      f.options,
      f.environment,
    ),
    { code: "write_failed" },
  );
  assert.equal(f.disconnects(), 1);
  assert.deepEqual(f.revoked, ["blob:image-1"]);
});

test("uncaught erase failures release the port and never count as success", async () => {
  const f = fixture();
  await assert.rejects(
    flashSession(
      async () => {
        throw new Error("erase failed");
      },
      f.options,
      f.environment,
    ),
    /erase failed/,
  );
  assert.equal(f.disconnects(), 1);
});

test("cleanup failures require reload even after the engine reports completion", async () => {
  const f = fixture();
  f.environment.esploader.transport.disconnect = async () => {
    throw new Error("locked");
  };
  await assert.rejects(
    flashSession(
      async (notify) => notify({ state: "finished" }),
      f.options,
      f.environment,
    ),
    { code: "disconnect_failed", requiresReload: true },
  );
});

test("does not close an unrelated loader or a port already closed by the engine", async () => {
  const f = fixture();
  f.options.port.readable = null;
  f.options.port.writable = null;
  await flashSession(
    async (notify) => notify({ state: "finished" }),
    f.options,
    f.environment,
  );
  assert.equal(f.disconnects(), 0);
  const other = fixture();
  other.environment.esploader.transport.device = {};
  await assert.rejects(
    flashSession(async () => {}, other.options, other.environment),
    /安装未完成/,
  );
  assert.equal(other.disconnects(), 0);
  assert.ok(other.environment.esploader);
});
