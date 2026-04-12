import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [tailwindcss(), react()],
  base: '/',
  build: {
    outDir: '../spiffs_image',
    emptyOutDir: true,
    // Mniejsze chunki - SPIFFS nie obsługuje plików > 512KB w zależności od konfiguracji
    chunkSizeWarningLimit: 500,
    rollupOptions: {
      output: {
        manualChunks: {
          vendor: ['react', 'react-dom', 'react-router-dom'],
        },
      },
    },
  },
  server: {
    // Proxy do ESP32 podczas developmentu
    proxy: {
      '/api': {
        target: 'http://192.168.20.230',
        changeOrigin: true,
      },
    },
  },
})
