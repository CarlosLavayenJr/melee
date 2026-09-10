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
set $ftr = (Fighter*) gobj->user_data
printf "  kind          = %d\n", $ftr->kind
printf "  cur_pos       = %f %f %f\n", $ftr->cur_pos.x, $ftr->cur_pos.y, $ftr->cur_pos.z
printf "  prev_pos      = %f %f %f\n", $ftr->prev_pos.x, $ftr->prev_pos.y, $ftr->prev_pos.z
printf "  self_vel      = %f %f %f\n", $ftr->self_vel.x, $ftr->self_vel.y, $ftr->self_vel.z
printf "  scale         = %f %f %f\n", $ftr->x34_scale.x, $ftr->x34_scale.y, $ftr->x34_scale.z
printf "  cam_box pos   = %f %f %f\n", $ftr->x890_cameraBox->pos.x, $ftr->x890_cameraBox->pos.y, $ftr->x890_cameraBox->pos.z
printf "  cam_box bone  = %f %f %f\n", $ftr->x890_cameraBox->bone_pos.x, $ftr->x890_cameraBox->bone_pos.y, $ftr->x890_cameraBox->bone_pos.z
# bone_pos is written by ftLib_800866DC as
#   lb_8000B1CC(ftLib_80086630(gobj, attrs->camera_zoom_target_bone),
#               &attrs->x170, v)
# which is a bone's matrix times a fixed offset. The first run of this probe
# showed cur_pos, prev_pos and self_vel all sane -- the fighter was falling
# normally -- with only bone_pos NaN, so the fault is in one of those two
# inputs. x170 and camera_zoom_target_bone both sit below
# weight_independent_throws_mask at 0x180 and so are inside the range
# attrs_to_native converts, which points at the bone matrix.
printf "  zoom bone idx = %d\n", $ftr->co_attrs.camera_zoom_target_bone
printf "  x170 offset   = %f %f %f\n", $ftr->co_attrs.x170.x, $ftr->co_attrs.x170.y, $ftr->co_attrs.x170.z
set $bj = ftLib_80086630(gobj, $ftr->co_attrs.camera_zoom_target_bone)
printf "  bone jobj     = %p\n", $bj
if $bj != 0
printf "  bone parent   = %p\n", $bj->parent
printf "  bone translate= %f %f %f\n", $bj->translate.x, $bj->translate.y, $bj->translate.z
printf "  bone scale    = %f %f %f\n", $bj->scale.x, $bj->scale.y, $bj->scale.z
printf "  bone mtx r0   = %f %f %f %f\n", $bj->mtx[0][0], $bj->mtx[0][1], $bj->mtx[0][2], $bj->mtx[0][3]
printf "  bone mtx r1   = %f %f %f %f\n", $bj->mtx[1][0], $bj->mtx[1][1], $bj->mtx[1][2], $bj->mtx[1][3]
printf "  bone mtx r2   = %f %f %f %f\n", $bj->mtx[2][0], $bj->mtx[2][1], $bj->mtx[2][2], $bj->mtx[2][3]
end
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
