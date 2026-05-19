import path from 'path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  // Use './' so the app works at any path depth (GitHub Pages subdir or Vercel root).
  base: './',
  resolve: {
    alias: { '@examples': path.resolve(__dirname, '../examples') },
  },
  server: {
    fs: { allow: [path.resolve(__dirname, '..')] },
  },
});
