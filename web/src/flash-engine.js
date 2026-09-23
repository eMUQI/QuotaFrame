import { flash } from "esp-web-tools/dist/flash.js";
import { flashSession } from "./flash-session.js";

export const runFlash = (options) => flashSession(flash, options);
