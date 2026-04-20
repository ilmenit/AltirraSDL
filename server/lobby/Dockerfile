# syntax=docker/dockerfile:1
FROM golang:1.22-alpine AS build
WORKDIR /src
COPY go.mod ./
COPY *.go ./
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" -o /out/altirra-lobby .

FROM scratch
COPY --from=build /out/altirra-lobby /altirra-lobby
EXPOSE 8080
USER 65532:65532
ENTRYPOINT ["/altirra-lobby"]
