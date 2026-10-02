// Mints a tester token: npm run -w cloud token -- <tester>
// Prints the token (give it to the tester once; it isn't stored anywhere) and the line for the service's tokens file.
import { hashToken, newToken } from "./access.ts";

const tester = process.argv[2] ?? "";
if (!/^[\w.-]{1,64}$/.test(tester)) {
  console.error("usage: npm run -w cloud token -- <tester>   (letters, digits, '.', '_' or '-')");
  process.exit(2);
}
const token = newToken();
console.log(`token for ${tester} (send it privately; it is shown only now):\n  ${token}\n`);
console.log(`line for the tokens file (FLOWSTATE_SERVICE_TOKENS_FILE):\n  ${tester} ${hashToken(token)}`);
