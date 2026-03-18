package org.citra.citra_emu.features.input

/**
 * Stores information about a particular input
 */
data class Input(
    val key: Int? = null,
    val axis: Int? = null,
    // +1 or -1
    val direction: Int? = null,
    val threshold: Float? = null
) {
    val empty: Boolean
        get() =  key == null && axis == null

}