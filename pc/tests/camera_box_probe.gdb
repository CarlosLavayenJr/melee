# Run from repository root:
#   gdb -batch -x pc/tests/camera_box_probe.gdb build/phase2/melee_host.exe
#
# Same scripted VS route as match_smoke.gdb, but instead of only reporting
# that lbvector.c:383 asserted, this prints the numbers that produced the
# position it refused.
#
# Camera_80030CFC normalizes a direction and scales it by
# `cam_box->ext.v.z + tolerance`, so the assert fires either because that
# extent is a large float or because the direction it normalized was zero
# length and the divide produced an infinity. Those need different fixes and
# the assert cannot tell them apart, so both inputs are printed here.
#
# The breakpoint carries the assert's own condition, negated. Without it the
# breakpoint fires on every call -- the HSD_ASSERT line is evaluated whether
# or not it fails -- and the first run of this probe stopped on a perfectly
# healthy call reached from a different caller, whose frame had no cam_box to
# print.
set pagination off
set print thread-events off
set environment PC_INPUT_SCRIPT vs
break main
run
set capture_attempted = 1
delete 1

break lbvector.c:383 if !(pos3d->x > -50000.0F && pos3d->x < 50000.0F)
commands
silent
printf "PROBE: lbVector_WorldToScreen refused a position\n"
printf "  pos3d = %f %f %f\n", pos3d->x, pos3d->y, pos3d->z
bt 6
up
up
printf "  cam_box       = %p\n", cam_box
printf "  bone_pos      = %f %f %f\n", cam_box->bone_pos.x, cam_box->bone_pos.y, cam_box->bone_pos.z
printf "  pos           = %f %f %f\n", cam_box->pos.x, cam_box->pos.y, cam_box->pos.z
printf "  ext.v         = %f %f %f\n", cam_box->ext.v.x, cam_box->ext.v.y, cam_box->ext.v.z
printf "  ext.h         = %f %f\n", cam_box->ext.h.x, cam_box->ext.h.y
printf "  target_ext.v  = %f %f %f\n", cam_box->target_ext.v.x, cam_box->target_ext.v.y, cam_box->target_ext.v.z
printf "  tolerance     = %f\n", tolerance
printf "  range         = %f\n", range
printf "  sp38          = %f %f %f\n", sp38.x, sp38.y, sp38.z
printf "  sp20          = %f %f %f\n", sp20.x, sp20.y, sp20.z
quit 0
end

# Anything else that stops the run should still end the test rather than hang.
break __assert
break abort
break pc_sys_exit
break OSPanic
break panicMissingStageParam

continue
printf "PROBE: stopped somewhere else\n"
bt 8
quit 1
