const CACHE = "pocket-scion-editor-v8";
self.addEventListener("install", event => event.waitUntil(
  caches.open(CACHE)
    .then(cache => cache.addAll(["./", "./index.html", "./manifest.webmanifest", "./icon.svg"]))
    .then(() => self.skipWaiting()),
));
self.addEventListener("activate", event => event.waitUntil(
  caches.keys()
    .then(keys => Promise.all(keys.filter(key => key !== CACHE).map(key => caches.delete(key))))
    .then(() => self.clients.claim()),
));
self.addEventListener("fetch", event => {
  const request = event.request;
  if (request.mode === "navigate") {
    event.respondWith(fetch(request).then(response => {
      caches.open(CACHE).then(cache => cache.put("./index.html", response.clone()));
      return response;
    }).catch(() => caches.match("./index.html")));
    return;
  }
  event.respondWith(caches.match(request).then(hit => hit || fetch(request).then(response => {
    if (request.method === "GET" && new URL(request.url).origin === self.location.origin) {
      caches.open(CACHE).then(cache => cache.put(request, response.clone()));
    }
    return response;
  })));
});
