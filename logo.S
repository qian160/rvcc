.section .rodata
.global logo
.global usage_msg

logo:
.incbin "./logo.txt"
.byte 0

usage_msg:
.incbin "./usage.txt"
.byte 0
