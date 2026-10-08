const NATIVE = (Deno.env.get("ENGCODER_NATIVE_URL") ?? "http://127.0.0.1:14321").replace(/\/+$/, "");
const WEB_ROOT = new URL("../", import.meta.url);

function staticContentType(path: string): string {
  if (path.endsWith(".js")) return "text/javascript; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  return "text/html; charset=utf-8";
}

async function proxyToNative(request: Request, url: URL): Promise<Response> {
  const target = new URL(`${url.pathname}${url.search}`, `${NATIVE}/`);
  const headers = new Headers(request.headers);
  headers.delete("host");

  const init: RequestInit = {
    method: request.method,
    headers,
    redirect: "manual",
  };
  if (request.method !== "GET" && request.method !== "HEAD") {
    init.body = await request.arrayBuffer();
  }

  try {
    const upstream = await fetch(target, init);
    return new Response(upstream.body, {
      status: upstream.status,
      statusText: upstream.statusText,
      headers: upstream.headers,
    });
  } catch (error) {
    return Response.json({
      error: `EngCoder native runtime is unavailable at ${NATIVE}: ${String(error)}`,
    }, { status: 503 });
  }
}

Deno.serve({ hostname: "127.0.0.1", port: 8788 }, async (request) => {
  const url = new URL(request.url);

  if (url.pathname === "/") {
    return Response.redirect(new URL("/ui/", request.url), 302);
  }

  if (url.pathname === "/health" || url.pathname.startsWith("/api/")) {
    return await proxyToNative(request, url);
  }

  const relativePath = url.pathname === "/ui/"
    ? "index.html"
    : url.pathname.startsWith("/ui/")
    ? url.pathname.slice(4)
    : "";

  if (!relativePath || relativePath.includes("..") || relativePath.startsWith("/")) {
    return new Response("Not found", { status: 404 });
  }

  try {
    const bytes = await Deno.readFile(new URL(relativePath, WEB_ROOT));
    return new Response(bytes, {
      headers: {
        "content-type": staticContentType(relativePath),
        "cache-control": "no-store",
      },
    });
  } catch {
    return new Response("Not found", { status: 404 });
  }
});
