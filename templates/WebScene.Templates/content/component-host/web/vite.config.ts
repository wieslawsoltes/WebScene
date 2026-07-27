import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { webscene } from '@webscene/sdk/vite';

export default defineConfig({
  define: { 'process.env.NODE_ENV': JSON.stringify('production') },
  plugins: [webscene({ manifest: 'webscene-component.json' }), react()],
  build: { outDir: '../Component', emptyOutDir: true, lib: { entry: 'src/main.tsx', formats: ['iife'], name: 'WebSceneComponent', fileName: () => 'dist/main.js' } }
});
