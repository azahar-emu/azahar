# Moving the Android touch controls

## Provenance and validation

The implementation on this personal fork was generated with AI assistance
(OpenAI Codex), including the Kotlin changes and documentation. It is not an
official Azahar release. The upstream [AI policy](../../AI-POLICY.md) does not
permit submitting a substantial AI-written contribution as-is.

The Android Kotlin sources and the ARM64 `vanillaRelWithDebInfoLite` APK compiled
successfully. Kotlin formatting (`:app:ktlintCheck`), APK signature and ZIP alignment
checks passed. The APK uses the
`org.azahar_emu.azahar.debug` application ID and can coexist with the official
Vanilla release. Device and gameplay checks listed below have not been performed.

## Usage

While a game is open, open the in-game menu and select **Move touch controls**
(**Mover controles de toque** in Brazilian Portuguese). Drag a button, D-pad,
circle pad or C-stick to its new position, then select **Done** or press Back.
The existing **Overlay options > Edit Layout** entry opens the same editor.

Positions are saved when each drag ends. Portrait and landscape layouts are
stored separately using the existing preferences. Dragging is limited to the
overlay bounds. Controls are visible while editing even when the overlay is
hidden or its opacity is zero; those display preferences are restored on exit.
Individually disabled controls can be enabled through Overlay options first.
The existing reset overlay option restores the current orientation's layout.

## Device verification

These checks require an Android build and a running game:

1. Move R, A, the D-pad, circle pad and C-stick individually. Each control should
   follow the finger without jumping, and retain its size.
2. Place two controls on top of one another. Only the topmost control should move
   on the next drag. Drag beyond each screen edge; the control should stay visible.
3. While dragging, put another finger on a different control, move both fingers,
   and lift the second finger. Only the first control should move; its drag should
   continue. Also try starting a drag with a second finger while the first finger
   rests on an empty area.
4. Exit with Done and with Back. Test all moved controls during gameplay. The
   analog sticks should return to their new centers when released; the D-pad's
   pressed images should rotate around its new center.
5. Close and reopen the game. Confirm that the positions persist. Repeat in the
   other orientation and confirm each orientation retains its own layout.
6. Rotate or background the app during a drag, then reopen the editor. Confirm
   that the control is not stuck to the next touch and its last position is saved
   for the original orientation.
7. Hide the overlay, and separately set opacity to zero. Enter the editor, move a
   control, and exit. Confirm visibility/opacity preferences are restored.
8. Reset the overlay and reopen the game. Confirm the current orientation uses
   default positions and the other orientation remains unchanged.
