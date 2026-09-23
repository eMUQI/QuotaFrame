/** Run the pinned ESP Web Tools engine without its dialog. */

const SHA256_HEX = /^[0-9a-f]{64}$/;

function isValidPart(part) {
  return (
    typeof part.path === "string" &&
    Number.isInteger(part.offset) &&
    part.offset >= 0 &&
    typeof part.sha256 === "string" &&
    SHA256_HEX.test(part.sha256)
  );
}

async function sha256Hex(environment, bytes) {
  const digest = await environment.crypto.subtle.digest("SHA-256", bytes);
  return Array.from(new Uint8Array(digest), (byte) =>
    byte.toString(16).padStart(2, "0"),
  ).join("");
}

/**
 * Downloads every image the manifest declares and checks it against the
 * digest the release assembler recorded. Build-time verification only covers
 * what the server holds; a truncated or tampered response would otherwise be
 * written to the device unchecked. Verified bytes are handed back as object
 * URLs because the engine resolves part paths itself and would otherwise
 * download each image a second time.
 */
async function verifyBuilds(manifest, manifestUrl, environment, objectUrls) {
  const builds = await Promise.all(
    manifest.builds.map(async (build) => ({
      ...build,
      parts: await Promise.all(
        build.parts.map(async (part) => {
          const response = await environment.fetch(
            new URL(part.path, manifestUrl).toString(),
          );
          if (!response.ok) throw new Error("Firmware download failed");
          const bytes = await response.arrayBuffer();
          if ((await sha256Hex(environment, bytes)) !== part.sha256) {
            throw Object.assign(new Error("固件校验失败"), {
              code: "firmware_checksum_failed",
            });
          }
          const objectUrl = environment.URL.createObjectURL(
            new environment.Blob([bytes]),
          );
          objectUrls.push(objectUrl);
          return { ...part, path: objectUrl };
        }),
      ),
    })),
  );
  return { ...manifest, builds };
}

export async function flashSession(
  flash,
  { port, manifestUrl, eraseFirst, onEvent },
  environment = globalThis,
) {
  let manifest;
  try {
    const response = await environment.fetch(manifestUrl, {
      cache: "no-store",
    });
    if (!response.ok) throw new Error("Manifest download failed");
    manifest = await response.json();
    if (
      !Array.isArray(manifest.builds) ||
      !manifest.builds.length ||
      manifest.builds.some(
        (build) =>
          !build.chipFamily ||
          !Array.isArray(build.parts) ||
          !build.parts.length ||
          !build.parts.every(isValidPart),
      )
    )
      throw new Error("Invalid manifest");
  } catch {
    throw Object.assign(new Error("固件清单格式不正确"), {
      code: "fetch_manifest_failed",
    });
  }

  const objectUrls = [];
  let verified;
  try {
    onEvent({ state: "preparing", details: { done: false } });
    verified = await verifyBuilds(
      manifest,
      manifestUrl,
      environment,
      objectUrls,
    );
  } catch (error) {
    for (const url of objectUrls) environment.URL.revokeObjectURL(url);
    throw error.code
      ? error
      : Object.assign(new Error("固件下载失败"), {
          code: "failed_firmware_download",
        });
  }

  let failure;
  let finished = false;
  try {
    await flash(
      (event) => {
        if (event.state === "error") {
          failure = Object.assign(new Error(event.message), {
            code: event.details?.error,
          });
        } else if (event.state === "finished") {
          finished = true;
        }
        onEvent(event);
      },
      port,
      manifestUrl,
      verified,
      eraseFirst,
    );
  } catch (error) {
    failure ??= error;
  } finally {
    for (const url of objectUrls) environment.URL.revokeObjectURL(url);
    // ESP Web Tools 10.4.0 exposes its loader here; reset failures can bypass its disconnect.
    const transport = environment.esploader?.transport;
    if (transport?.device === port) {
      try {
        if (port.readable || port.writable) await transport.disconnect();
      } catch (error) {
        failure ??= Object.assign(error, { code: "disconnect_failed" });
        failure.requiresReload = true;
      }
      delete environment.esploader;
    }
  }
  if (failure) throw failure;
  if (!finished) throw new Error("安装未完成，请重新连接设备。");
}
