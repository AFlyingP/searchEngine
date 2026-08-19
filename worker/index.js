export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // Proxy REST API calls to the live C++ backend tunnel
    if (url.pathname.startsWith('/api/')) {
      const backend = env.BACKEND_URL || "https://persistent-mambo-shelf-correctly.trycloudflare.com";
      try {
        const targetUrl = new URL(url.pathname + url.search, backend);
        const proxyReq = new Request(targetUrl.toString(), {
          method: request.method,
          headers: request.headers,
          body: (request.method !== 'GET' && request.method !== 'HEAD') ? request.body : undefined
        });
        const resp = await fetch(proxyReq);
        const newHeaders = new Headers(resp.headers);
        newHeaders.set('Access-Control-Allow-Origin', '*');
        return new Response(resp.body, {
          status: resp.status,
          statusText: resp.statusText,
          headers: newHeaders
        });
      } catch (err) {
        return new Response(JSON.stringify({ error: "Backend search engine currently offline." }), {
          status: 502,
          headers: { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" }
        });
      }
    }

    // Serve static frontend assets from web/
    return env.ASSETS.fetch(request);
  }
};
