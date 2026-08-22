import createModule from "./saturn.js";

const Module = await createModule();
console.log("Module loaded OK");
const ok = Module.ccall("saturn_init", "number", [], []);
console.log("saturn_init() ->", ok);
