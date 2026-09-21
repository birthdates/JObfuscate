package com.kotest

// Kotlin obfuscation fixture: data classes, objects, top-level functions and
// string templates exercise renaming, string encryption, control flow, the
// kotlin.Metadata stripper and (with --import-java) MH proxies over stdlib.

data class Vault(val owner: String, var secrets: Int)

fun scramble(input: String, key: Int): String {
    val sb = StringBuilder()
    for (c in input) {
        sb.append((c.code xor key).toChar())
    }
    return sb.toString()
}

fun describeSecret(n: Int): String = when {
    n <= 0 -> "empty"
    n < 10 -> "small"
    else -> "large"
}

object Keeper {
    const val TAG = "KOTLIN_VAULT_2025"

    fun describe(v: Vault): String =
        "$TAG:${v.owner}:${v.secrets}:${describeSecret(v.secrets)}"
}

fun main() {
    val v = Vault("alice", 7)
    println(Keeper.describe(v))
    println(scramble("hello", 3))
    println("KOTLIN-SUITES-PASSED")
}
