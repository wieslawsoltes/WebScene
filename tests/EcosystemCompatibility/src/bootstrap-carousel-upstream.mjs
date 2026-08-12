import "./jasmine-lite.mjs";
import "hammer-simulator";
import "../upstream/bootstrap/js/tests/unit/carousel.spec.js";
import { runRegisteredJasmineTests } from "./jasmine-lite.mjs";

runRegisteredJasmineTests();
