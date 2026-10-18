package fr.f4lek.techoaprs.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

private val Light = lightColorScheme(
    primary = Color(0xFF2E7D32), onPrimary = Color.White,
    primaryContainer = Color(0xFFC8E6C9), onPrimaryContainer = Color(0xFF0B3D0E),
    secondary = Color(0xFF5D4037), tertiary = Color(0xFF1565C0),
)
private val Dark = darkColorScheme(
    primary = Color(0xFF81C784), onPrimary = Color(0xFF0B3D0E),
    primaryContainer = Color(0xFF1B5E20), onPrimaryContainer = Color(0xFFC8E6C9),
    secondary = Color(0xFFBCAAA4), tertiary = Color(0xFF90CAF9),
)

@Composable
fun TechoTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val ctx = LocalContext.current
    val scheme = when {
        Build.VERSION.SDK_INT >= 31 -> if (dark) dynamicDarkColorScheme(ctx) else dynamicLightColorScheme(ctx)
        dark -> Dark
        else -> Light
    }
    MaterialTheme(colorScheme = scheme, content = content)
}
