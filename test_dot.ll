; ModuleID = 'flux_module'
source_filename = "flux_module"

@.fmt.float = private unnamed_addr constant [4 x i8] c"%g\0A\00", align 1

declare i32 @printf(ptr, ...)

define double @dot(i32 %n) {
entry:
  %i = alloca i32, align 4
  %sum = alloca double, align 8
  %n1 = alloca i32, align 4
  store i32 %n, ptr %n1, align 4
  store double 0.000000e+00, ptr %sum, align 8
  store i32 0, ptr %i, align 4
  br label %while.cond

while.cond:                                       ; preds = %while.body, %entry
  %i2 = load i32, ptr %i, align 4
  %n3 = load i32, ptr %n1, align 4
  %cmp = icmp slt i32 %i2, %n3
  br i1 %cmp, label %while.body, label %while.exit

while.body:                                       ; preds = %while.cond
  %sum4 = load double, ptr %sum, align 8
  %add = fadd double %sum4, 1.000000e+00
  store double %add, ptr %sum, align 8
  %i5 = load i32, ptr %i, align 4
  %add6 = add i32 %i5, 1
  store i32 %add6, ptr %i, align 4
  br label %while.cond

while.exit:                                       ; preds = %while.cond
  %sum7 = load double, ptr %sum, align 8
  ret double %sum7
}

define i32 @main() {
entry:
  %dot.ret = call double @dot(i32 100)
  %0 = call i32 (ptr, ...) @printf(ptr @.fmt.float, double %dot.ret)
  ret i32 0
}
