# =====================================================
# 地动仪前端 Web 服务 (Nginx + Gzip/Brotli)
# =====================================================

FROM nginx:1.27-alpine AS runtime

LABEL maintainer="seismograph-team" \
      description="古代地动仪系统前端 - Nginx 静态资源服务 + Gzip 压缩 + API 代理"

RUN apk add --no-cache \
        brotli \
        curl \
        tzdata \
 && cp /usr/share/zoneinfo/Asia/Shanghai /etc/localtime \
 && echo "Asia/Shanghai" > /etc/timezone \
 && apk del tzdata \
 && rm -rf /var/cache/apk/*

COPY frontend/index.html  /usr/share/nginx/html/index.html
COPY frontend/css/        /usr/share/nginx/html/css/
COPY frontend/js/         /usr/share/nginx/html/js/

RUN if [ -d /usr/share/nginx/html/css ]; then \
        find /usr/share/nginx/html/css -name '*.css' -exec brotli -f -k -q 11 {} \; ; \
    fi \
 && if [ -d /usr/share/nginx/html/js ]; then \
        find /usr/share/nginx/html/js  -name '*.js'  -exec brotli -f -k -q 11 {} \; ; \
    fi \
 && if [ -f /usr/share/nginx/html/index.html ]; then \
        brotli -f -k -q 11 /usr/share/nginx/html/index.html; \
    fi

COPY frontend/nginx.conf /etc/nginx/nginx.conf

EXPOSE 80

HEALTHCHECK --interval=30s --timeout=5s --retries=3 --start-period=5s \
    CMD curl -f http://127.0.0.1/ || exit 1

CMD ["nginx", "-g", "daemon off;"]
