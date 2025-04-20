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

LDA $209
SAR $1

LDA $0
SAR $2

LDA $0
SAR $3

out:
LDA $0
line:
PSH @0
ADD @4
SAM @1,2
POP $0
?incr_reg 2
?incr_reg_carry 1
SAM @1,2
PSH @0
LDA @3
?incr_reg 2
?incr_reg_carry 1
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

loop:
LDA $1
SAM $128,00
JAL loop
