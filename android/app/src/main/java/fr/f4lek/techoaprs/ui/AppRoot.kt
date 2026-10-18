package fr.f4lek.techoaprs.ui

import fr.f4lek.techoaprs.R
import fr.f4lek.techoaprs.L
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Chat
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.BluetoothSearching
import androidx.compose.material.icons.filled.Landscape
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Badge
import androidx.compose.material3.BadgedBox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import fr.f4lek.techoaprs.Techo
import fr.f4lek.techoaprs.ble.LinkState
import kotlinx.coroutines.flow.MutableStateFlow

val LocalTecho = staticCompositionLocalOf<Techo> { error("no Techo") }
val LocalSnack = staticCompositionLocalOf<SnackbarHostState> { error("no snackbar") }

private data class Tab(val route: String, val label: Int, val icon: androidx.compose.ui.graphics.vector.ImageVector)

private val tabs = listOf(
    Tab("sota", R.string.tab_sota, Icons.Filled.Landscape),
    Tab("convs", R.string.tab_messages, Icons.AutoMirrored.Filled.Chat),
    Tab("config", R.string.tab_settings, Icons.Filled.Settings),
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppRoot(techo: Techo, openPeer: MutableStateFlow<String?>) {
    val nav = rememberNavController()
    val snack = remember { SnackbarHostState() }
    var showConnect by rememberSaveable { mutableStateOf(false) }
    val linkState by techo.link.state.collectAsState()
    val msgVersion by techo.messages.version.collectAsState()
    val unread = remember(msgVersion) { techo.messages.conversations().sumOf { it.unread } }
    val backStack by nav.currentBackStackEntryAsState()
    val route = backStack?.destination?.route ?: "sota"
    val peer by openPeer.collectAsState()

    LaunchedEffect(peer) {
        peer?.let {
            nav.navigate("chat/$it") { launchSingleTop = true }
            openPeer.value = null
        }
    }

    CompositionLocalProvider(LocalTecho provides techo, LocalSnack provides snack) {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        Text(
                            when {
                                route.startsWith("chat/") -> backStack?.arguments?.getString("peer") ?: ""
                                route == "convs" -> L.s(R.string.tab_messages)
                                route == "config" -> L.s(R.string.title_settings)
                                else -> L.s(R.string.title_sota)
                            }
                        )
                    },
                    navigationIcon = {
                        if (route.startsWith("chat/")) IconButton(onClick = { nav.popBackStack() }) {
                            Icon(Icons.AutoMirrored.Filled.ArrowBack, L.s(R.string.back))
                        }
                    },
                    actions = { LinkChip(linkState) { showConnect = true } },
                )
            },
            bottomBar = {
                if (!route.startsWith("chat/")) NavigationBar {
                    for (t in tabs) NavigationBarItem(
                        selected = route == t.route,
                        onClick = {
                            nav.navigate(t.route) {
                                popUpTo(nav.graph.findStartDestination().id) { saveState = true }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                        icon = {
                            if (t.route == "convs" && unread > 0) BadgedBox(badge = { Badge { Text("$unread") } }) {
                                Icon(t.icon, null)
                            } else Icon(t.icon, null)
                        },
                        label = { Text(L.s(t.label)) },
                    )
                }
            },
            snackbarHost = { SnackbarHost(snack) },
        ) { pad ->
            NavHost(nav, startDestination = "sota", modifier = Modifier.padding(pad)) {
                composable("sota") { SotaScreen { showConnect = true } }
                composable("convs") { ConversationsScreen(onOpen = { nav.navigate("chat/$it") }) }
                composable("chat/{peer}", arguments = listOf(navArgument("peer") { type = NavType.StringType })) {
                    ChatScreen(it.arguments?.getString("peer") ?: "")
                }
                composable("config") { ConfigScreen { showConnect = true } }
            }
        }
        if (showConnect) ConnectSheet(onDismiss = { showConnect = false })
    }
}

@Composable
private fun LinkChip(state: LinkState, onClick: () -> Unit) {
    val (label, icon) = when (state) {
        is LinkState.Ready -> state.name to Icons.Filled.BluetoothConnected
        is LinkState.Connecting -> L.s(R.string.chip_connecting) to Icons.Filled.BluetoothSearching
        is LinkState.Scanning -> L.s(R.string.chip_scanning) to Icons.Filled.BluetoothSearching
        is LinkState.Failed -> L.s(R.string.error) to Icons.Filled.Bluetooth
        LinkState.Idle -> L.s(R.string.connect) to Icons.Filled.Bluetooth
    }
    AssistChip(
        onClick = onClick,
        label = { Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis) },
        leadingIcon = { Icon(icon, null, Modifier) },
        colors = if (state is LinkState.Ready) AssistChipDefaults.assistChipColors(
            containerColor = MaterialTheme.colorScheme.primaryContainer
        ) else AssistChipDefaults.assistChipColors(),
        modifier = Modifier.padding(end = 8.dp),
    )
}
