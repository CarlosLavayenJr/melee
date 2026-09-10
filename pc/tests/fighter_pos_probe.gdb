# Run from repository root:
#   gdb -batch -x pc/tests/fighter_pos_probe.gdb build/phase2/melee_host.exe
#
# With the camera's adjacent-globals bug fixed, matches run to about 73 frames
# and then stop at the same lbvector.c:383 assert -- but through a different
# path. It is no longer the derived position from Camera_80030CFC; it is
# Camera_80030CD8, which passes a fighter's camera-box position straight in:
#
#   Camera_80030CD8(cam_box, arg1) -> Camera_80030BBC(&cam_box->bone_pos, ...)
#
# So this time the subject's own position is out of range, rather than a
# camera-space vector derived from it. Two very different causes are possible
# and the assert cannot tell them apart:
#
#   - a real number that grew, which means physics: a fighter accelerating off
#     the stage because an attribute it is integrating is wrong;
#   - a NaN, which means something upstream divided badly, as the camera did.
#
# Printing the position together with the velocity and the previous position
# separates them at a glance: a launched fighter has a large velocity and a
# position that grew smoothly, while a NaN appears in one frame with the
# previous position still sane.
#
# The breakpoint carries the assert's own condition, negated, for the reason
# spelled out in camera_box_probe.gdb: an HSD_ASSERT line is evaluated on
# every call, so an unconditional breakpoint stops on healthy ones.
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
printf "PROBE: a subject position was refused\n"
printf "  pos3d = %f %f %f\n", pos3d->x, pos3d->y, pos3d->z
bt 8
up
up
up
printf "  --- frame ftLib_80086A8C ---\n"
set $fp = (Fighter*) gobj->user_data
printf "  kind          = %d\n", $fp->kind
printf "  cur_pos       = %f %f %f\n", $fp->cur_pos.x, $fp->cur_pos.y, $fp->cur_pos.z
printf "  prev_pos      = %f %f %f\n", $fp->prev_pos.x, $fp->prev_pos.y, $fp->prev_pos.z
printf "  self_vel      = %f %f %f\n", $fp->self_vel.x, $fp->self_vel.y, $fp->self_vel.z
printf "  scale         = %f %f %f\n", $fp->x34_scale.x, $fp->x34_scale.y, $fp->x34_scale.z
printf "  cam_box pos   = %f %f %f\n", $fp->x890_cameraBox->pos.x, $fp->x890_cameraBox->pos.y, $fp->x890_cameraBox->pos.z
printf "  cam_box bone  = %f %f %f\n", $fp->x890_cameraBox->bone_pos.x, $fp->x890_cameraBox->bone_pos.y, $fp->x890_cameraBox->bone_pos.z
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
