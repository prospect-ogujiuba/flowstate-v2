import preact from "@preact/preset-vite";
import { resolve } from "node:path";
import { defineConfig } from "vite";

// Two pages: the plugin UI (index.html) and the component gallery (gallery.html). The plugin's
// resource provider looks files up by name with no directories, so the output is flat.
export default defineConfig({
  plugins: [preact()],
  base: "./",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    assetsDir: "",
    target: "safari15",
    rollupOptions: {
      input: { index: resolve(import.meta.dirname, "index.html"), gallery: resolve(import.meta.dirname, "gallery.html") },
      // JUCE's interop library is served next to the page by the plugin (plugin/cmake/juce-targets.cmake).
      external: ["juce-interop"],
      output: { paths: { "juce-interop": "./juce_interop.js" } },
    },
  },
});
