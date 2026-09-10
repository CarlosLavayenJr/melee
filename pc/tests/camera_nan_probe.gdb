# Run from repository root:
#   gdb -batch -x pc/tests/camera_nan_probe.gdb build/phase2/melee_host.exe
#
# The follow-up to camera_box_probe.gdb. That one established that the camera
# box handed to Camera_80030CFC is healthy and that the NaN arrives through
# sp38, which is the output of
#
#   lbVector_8000E838(&interest, &eye_pos, &cam_box->bone_pos, &sp38)
#
# That function guards its only division against a near-zero length, so one of
# `interest` and `eye_pos` was already NaN on the way in. Both come from the
# game camera itself -- HSD_CObjGetEyePosition and HSD_CObjGetInterest on
# game_camera.gobj -- and neither depends on where a fighter is standing.
#
# This prints them and the two HSD_WObj positions they are read out of, so the
# next step is "which field of the camera went NaN" rather than "the camera
# is NaN".
#
# The breakpoint carries the assert's own condition, negated. A line
# breakpoint on an HSD_ASSERT fires on every call, including the healthy ones;
# an earlier probe stopped on one of those and printed a frame with no camera
# in it.
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
printf "PROBE: the camera's own vectors at the moment of the refusal\n"
bt 6
up
up
printf "  cobj                = %p\n", cobj
printf "  eye_pos             = %f %f %f\n", eye_pos.x, eye_pos.y, eye_pos.z
printf "  interest            = %f %f %f\n", interest.x, interest.y, interest.z
printf "  cobj->eyepos->pos   = %f %f %f\n", cobj->eyepos->pos.x, cobj->eyepos->pos.y, cobj->eyepos->pos.z
printf "  cobj->interest->pos = %f %f %f\n", cobj->interest->pos.x, cobj->interest->pos.y, cobj->interest->pos.z
printf "  cobj->near far      = %f %f\n", cobj->near, cobj->far
printf "  cobj->u.roll        = %f\n", cobj->u.roll
printf "  game_camera.translation = %f %f\n", game_camera.translation.x, game_camera.translation.y
quit 0
end

break __assert
break abort
break pc_sys_exit
break OSPanic
break panicMissingStageParam

continue
printf "PROBE: stopped somewhere else\n"
bt 8
quit 1
