const fs = require("fs");
const path = require("path");

let out = 0;
function list(dir = "./", pathNames = []) {
  for (const pathName of pathNames) {
    const p = path.join(dir, pathName);
    const s = fs.statSync(p);
    if (s.isDirectory()) {
      list(p, fs.readdirSync(p));
    } else {
      if (
        [
          ".hpp",
          ".cpp",
          ".js",
          ".lua",
          ".md",
          ".ini",
          ".txt",
          ".html",
          ".bat",
        ].includes(path.extname(p))
      ) {
        out += fs.readFileSync(p).toString("utf-8").split("\n").length;
      }
    }
  }
}

list(
  "./",
  fs
    .readdirSync("./")
    .filter((p) => p != ".git" && p != ".vscode" && p != ".pio")
);

console.log(out);
