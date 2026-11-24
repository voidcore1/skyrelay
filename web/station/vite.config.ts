/// <reference types="vitest/config" />
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// In development the station runs on Vite's server (npm run dev, port 5173)
// and everything that is not the app itself goes to a local gateway:
//
//   skyrelay-gateway --listen 127.0.0.1:8080 --web-root web/station/dist \
//                    --data-dir data --allow-origin http://localhost:5173
//
// changeOrigin rewrites Host to the gateway's address, which its Host check
// insists on. The WebSocket Origin stays http://localhost:5173, hence
// --allow-origin. In production the gateway serves dist/ on its own origin and
// needs neither.
const gateway = 'http://127.0.0.1:8080'

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/ws': { target: gateway, ws: true, changeOrigin: true },
      '/tiles': { target: gateway, changeOrigin: true },
      '/assets': { target: gateway, changeOrigin: true },
    },
  },
  build: {
    // The gateway serves /assets from its data directory (glyphs and
    // sprites), so the built app must not put its own files there.
    assetsDir: 'static',
    // MapLibre alone is over a megabyte minified, and the station is served
    // from loopback, so one big chunk is fine.
    chunkSizeWarningLimit: 1600,
  },
  test: {
    environment: 'jsdom',
  },
})
