?+incr_reg_carry
PSH @0
LDA @?0
ADD @C
SAR $?0
POP $0
?-

?+incr_reg
PSH @0
LDA @?0
ADD $1
SAR $?0
POP $0
?-

?+clear_reg
PSH @0
LDA $0
SAR $?0
POP $0
?-

loop:
LDA $0
SAR $1
SAR $2
SAR $3
SAR $4
SAR $5

// Input handling
LDA #128,5
AND $32
CMP $32
JNE right
// We pressed Left Pad
LDA #128,1
ADD $1
SAM $128,1
CMP $8
JNE start
LDA $0
SAM $128,1
?incr_reg 6
JAL start

right:
LDA #128,5
AND $8
CMP $8
JNE start
// We pressed Right pad
LDA #128,1
CMP $0
JEQ sub
ADD $255
SAM $128,1
CMP $248
JNE start
sub:
LDA $7
SAM $128,1

LDA @6
ADD $255
SAR $6

start:
LDA $209
SAR $1
out:
LDA $0
CMP $1
line:
PSH @0
ADD @4
SAM @1,2
POP $0

PSH @0
?incr_reg 2
?incr_reg_carry 1
// Sub @6
PSH @0
LDA @6
NOT $0
ADD $1
POP $7
ADD @7
SAM @1,2
POP $0

PSH @0
?incr_reg 2
?incr_reg_carry 1
LDA @3
SAM @1,2
POP $0
?incr_reg 2
?incr_reg_carry 1
ADD $1
CMP $16
JNE line

LDA @4
ADD $16
SAR $4
LDA @3
ADD $1
SAR $3
CMP $8
JNE out

LDA $1
SAM $128,00
JAL loop
