import { defineConfig } from 'vite'
import preact from '@preact/preset-vite'

export default defineConfig({
  plugins: [preact()],
  build: {
    // Output to data folder for ESP32
    outDir: '../data',
    emptyOutDir: false,
    // Optimize for smallest possible bundle
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true
      }
    },
    rollupOptions: {
      output: {
        // Single bundle, no code splitting
        manualChunks: undefined,
        // Simple filenames without hash for ESP32
        entryFileNames: 'app.js',
        assetFileNames: (assetInfo) => {
          if (assetInfo.name?.endsWith('.css')) {
            return 'style.css'
          }
          return '[name][extname]'
        }
      }
    },
    // Inline assets under 10KB
    assetsInlineLimit: 10240
  }
})
