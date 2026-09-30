import { defineConfig, devices } from "@playwright/test";

// Runs against the production build (`vite build` first; `npm test` does both).
export default defineConfig({
  testDir: "tests",
  fullyParallel: true,
  reporter: process.env.CI ? "github" : "list",
  use: { baseURL: "http://127.0.0.1:4174", viewport: { width: 1100, height: 800 } },
  // WebKit stands in for WKWebView (macOS hosts). It needs system libraries: CI installs them;
  // locally run `sudo npx playwright install-deps webkit` once, then set PW_WEBKIT=1.
  projects: [
    { name: "chromium", use: { ...devices["Desktop Chrome"], viewport: { width: 1100, height: 800 } } },
    ...(process.env.CI || process.env.PW_WEBKIT ? [{ name: "webkit", use: { ...devices["Desktop Safari"], viewport: { width: 1100, height: 800 } } }] : []),
  ],
  webServer: { command: "npx vite preview --port 4174 --strictPort --host 127.0.0.1", url: "http://127.0.0.1:4174/gallery.html", reuseExistingServer: !process.env.CI },
});
