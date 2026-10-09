## Plan: Animate makes the camera orbit its focus point

When Animate is on, the camera will circle the scene like a turntable, turning around the vertical axis. The center of the circle is the point the camera is looking at, at `FocusDistance`. One full turn takes 20 seconds, at any frame rate. The plan also fixes a bug: today, `AnimateCamera(false)` runs every frame and resets `cameraMovingRight_` and `cameraRotX_` to zero. That means the D key and mouse-look don't work while Animate is off.

**Steps**
1. **[ModelViewController.hpp](src/ModelViewController.hpp)**: Change the signature to `AnimateCamera(bool animate, float focusDistance, double timeDelta)`. Add two members: `bool isAnimating_{}` and `glm::vec3 orbitPivot_{}`.
2. **[ModelViewController.cpp](src/ModelViewController.cpp)**: Rewrite `AnimateCamera()`. *Depends on step 1.*
   - If `!animate`: set `isAnimating_ = false` and return `false`. Leave all other movement and rotation state alone (this is the bug fix).
   - When animation starts (`!isAnimating_`): set `orbitPivot_ = position_ + forward_ * focusDistance` and set `isAnimating_ = true`.
   - Each frame:
     - Compute `angle = 2π / 20 * timeDelta`.
     - Set `position_ = pivot + rotate(angle, Y) * (position_ - pivot)`.
     - Set `orientation_ = orientation_ * rotate(-angle, Y)`. This is the same as the existing `Rotate(-angle, 0)`.
     - Call `UpdateVectors()` and return `true`.
   - In `Reset()`: set `isAnimating_ = false`, so switching scenes picks a new center.
3. **[RayTracer.cpp](src/RayTracer.cpp#L180-L181)** in `Render()`: Call `AnimateCamera(userSettings_.Animate, userSettings_.FocusDistance, timeDelta)` and keep its result. Then set `resetAccumulation_ = UpdateCamera(...) || animated`. *Depends on step 2.*

**Why the camera keeps pointing at the center:** the camera's world transform is $C = T(p)\,O^{-1}$. Applying the orbit $C' = R\,C$ around the center gives $p' = c + R(p - c)$ and $O' = O\,R^{-1}$.

**Verification**
1. Run the "Build RayTracer" task.
2. Turn on Animate (checkbox or M key). The camera should circle the focus point and keep it centered on screen.
3. With Animate off, WASD, Shift/Ctrl and left-mouse look should all work, including D.
4. Turn Animate off, move the camera, and turn it back on. It should orbit around the new focus point.
5. Switch scenes while animating. The orbit should restart from the new scene's starting camera.
6. Accumulation should reset every frame while animating.

**Decisions**
- Rotation is around the world Y axis, so the camera moves horizontally in a ring, as you chose.
- The center is fixed when animation starts. Changing `FocusDistance` mid-orbit has no effect until Animate is toggled.
- Not included: speed setting in the UI, and benchmark mode (it already forces `Animate = false`).
