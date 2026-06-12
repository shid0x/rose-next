docker run --rm -it -p 8001:3000 `
    -e GO_ENV=development `
    -e DATABASE_URL=postgres://postgres:postgres@host.docker.internal/fider?sslmode=disable `
    -e JWT_SECRET="(*#c6#6ZM`MB@(gIV,9eFSLGy" `
    -e EMAIL_NOREPLY="noreply@rosenext.com" `
    -e EMAIL_SMTP_HOST=smtp.fastmail.com `
    -e EMAIL_SMTP_PORT=587 `
    -e EMAIL_SMTP_USERNAME="ralph@rednim.com" `
    -e EMAIL_SMTP_PASSWORD="wshmqvs25zujcs5u" `
    --name fider `
    getfider/fider:stable