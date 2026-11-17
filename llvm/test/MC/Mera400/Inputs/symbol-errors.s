.const a 1
.const a 2
.word .local
.local:
.org addr
.const addr 10
.cpu mera400
sint
.struct foo
.word 1 << 16
.word 1 * label
