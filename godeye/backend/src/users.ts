// GodEye user admin, run on the server PC:
//   node src/users.ts add <name> <HACKER|DISPATCH|OPERATOR|ADMIN>   (asks for the password, not echoed)
//   node src/users.ts remove <name>
//   node src/users.ts list
// [users.json] defaults to users.json next to this backend; GodEye picks changes up on its next start.
import { join, resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { ROLES, type Role, Users } from "./auth.ts";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const [cmd, name, role, file] = process.argv.slice(2);
const users = new Users(resolve(file ?? join(ROOT, "users.json")));

function askHidden(prompt: string): Promise<string> {
  return new Promise((done) => {
    process.stdout.write(prompt);
    const stdin = process.stdin;
    let text = "";
    if (stdin.isTTY) stdin.setRawMode(true);
    stdin.resume();
    stdin.setEncoding("utf8");
    const onData = (ch: string) => {
      for (const c of ch) {
        if (c === "\r" || c === "\n") { stdin.off("data", onData); if (stdin.isTTY) stdin.setRawMode(false); stdin.pause(); process.stdout.write("\n"); done(text); return; }
        if (c === "\u0003") process.exit(1);
        if (c === "\u007f" || c === "\b") text = text.slice(0, -1);
        else text += c;
      }
    };
    stdin.on("data", onData);
  });
}

if (cmd === "add" && name && ROLES.includes(role as Role)) {
  if (!/^[A-Za-z0-9_.-]{2,24}$/.test(name)) { console.log("names: 2-24 letters, digits, _ . -"); process.exit(1); }
  const p1 = await askHidden(`password for ${name}: `);
  const p2 = await askHidden("again: ");
  if (p1 !== p2 || p1.length < 8) { console.log("passwords differ or are shorter than 8 characters"); process.exit(1); }
  users.set(name, role as Role, p1);
  users.save();
  console.log(`${name} saved as ${role}`);
} else if (cmd === "remove" && name) {
  console.log(users.remove(name) ? (users.save(), `${name} removed`) : `no user ${name}`);
} else if (cmd === "list") {
  for (const u of users.list()) console.log(`${u.name}\t${u.role}`);
} else {
  console.log("usage: node src/users.ts add <name> <HACKER|DISPATCH|OPERATOR|ADMIN> | remove <name> | list");
  process.exit(1);
}
