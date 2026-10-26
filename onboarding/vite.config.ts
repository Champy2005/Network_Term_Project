import { defineConfig } from 'vitest/config';
export default defineConfig({
  base: './',
  server: { host: '127.0.0.1' },
  build: {
    target: 'es2022',
    chunkSizeWarningLimit: 800,
    rollupOptions: {
      output: { manualChunks: (id) => (id.includes('node_modules/three') ? 'three' : undefined) },
    },
  },
  test: { include: ['tests/**/*.test.ts'], environment: 'node' },
});
