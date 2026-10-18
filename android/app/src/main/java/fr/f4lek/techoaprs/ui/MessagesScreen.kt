package fr.f4lek.techoaprs.ui

import fr.f4lek.techoaprs.R
import fr.f4lek.techoaprs.L
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import fr.f4lek.techoaprs.data.ChatMessage
import fr.f4lek.techoaprs.data.MsgState
import fr.f4lek.techoaprs.proto.Aprs
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** APRS limits a message text to 67 characters (APRS101 ch. 14). */
const val APRS_MSG_MAX = 67

private val callRegex = Regex("^[A-Z0-9]{1,2}[0-9][A-Z0-9]{0,4}(-[0-9]{1,2})?$|^[A-Z0-9]{3,9}$")

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ConversationsScreen(onOpen: (String) -> Unit) {
    val techo = LocalTecho.current
    val version by techo.messages.version.collectAsState()
    val convs = remember(version) { techo.messages.conversations() }
    var newDialog by rememberSaveable { mutableStateOf(false) }
    var toDelete by remember { mutableStateOf<String?>(null) }

    Box(Modifier.fillMaxSize()) {
        if (convs.isEmpty()) {
            Column(Modifier.align(Alignment.Center).padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Text(L.s(R.string.msg_none), style = MaterialTheme.typography.titleMedium)
                Text(
                    L.s(R.string.msg_none_hint, techo.myCall),
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
        LazyColumn(Modifier.fillMaxSize()) {
            items(convs, key = { it.peer }) { c ->
                ListItem(
                    leadingContent = { Avatar(c.peer) },
                    headlineContent = { Text(c.peer, fontWeight = if (c.unread > 0) FontWeight.Bold else FontWeight.Normal) },
                    supportingContent = {
                        Text(
                            (if (c.last.outgoing) L.s(R.string.msg_me) else "") + c.last.text,
                            maxLines = 1, overflow = TextOverflow.Ellipsis,
                        )
                    },
                    trailingContent = {
                        Column(horizontalAlignment = Alignment.End) {
                            Text(shortTime(c.last.time), style = MaterialTheme.typography.labelSmall)
                            if (c.unread > 0) Box(
                                Modifier.padding(top = 4.dp).size(22.dp).background(MaterialTheme.colorScheme.primary, CircleShape),
                                contentAlignment = Alignment.Center,
                            ) { Text("${c.unread}", color = MaterialTheme.colorScheme.onPrimary, style = MaterialTheme.typography.labelSmall) }
                        }
                    },
                    modifier = Modifier.combinedClickable(onClick = { onOpen(c.peer) }, onLongClick = { toDelete = c.peer }),
                )
                HorizontalDivider()
            }
        }
        FloatingActionButton(
            onClick = { newDialog = true },
            modifier = Modifier.align(Alignment.BottomEnd).padding(16.dp),
        ) { Icon(Icons.Filled.Add, L.s(R.string.msg_new)) }
    }

    if (newDialog) {
        var call by remember { mutableStateOf("") }
        val ok = callRegex.matches(call.trim())
        AlertDialog(
            onDismissRequest = { newDialog = false },
            title = { Text(L.s(R.string.msg_new)) },
            text = {
                Column {
                    OutlinedTextField(
                        value = call,
                        onValueChange = { call = it.uppercase().take(9) },
                        label = { Text(L.s(R.string.msg_call_label)) },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
                    )
                    Spacer(Modifier.padding(4.dp))
                    TextButton(onClick = { call = Aprs.SOTA_GATEWAY }) { Text(L.s(R.string.msg_gateway)) }
                }
            },
            confirmButton = {
                TextButton(enabled = ok, onClick = { newDialog = false; onOpen(call.trim()) }) { Text(L.s(R.string.open)) }
            },
            dismissButton = { TextButton(onClick = { newDialog = false }) { Text(L.s(R.string.cancel)) } },
        )
    }
    toDelete?.let { peer ->
        AlertDialog(
            onDismissRequest = { toDelete = null },
            title = { Text(L.s(R.string.msg_delete_q, peer)) },
            confirmButton = {
                TextButton(onClick = { techo.messages.deleteConversation(peer); toDelete = null }) { Text(L.s(R.string.delete)) }
            },
            dismissButton = { TextButton(onClick = { toDelete = null }) { Text(L.s(R.string.cancel)) } },
        )
    }
}

@Composable
fun ChatScreen(peer: String) {
    val techo = LocalTecho.current
    val snack = LocalSnack.current
    val scope = rememberCoroutineScope()
    val version by techo.messages.version.collectAsState()
    val msgs = remember(version, peer) { techo.messages.messages(peer) }
    var draft by rememberSaveable { mutableStateOf("") }
    val listState = rememberLazyListState()

    DisposableEffect(peer) {
        techo.openConversation = peer
        onDispose { techo.openConversation = null }
    }
    LaunchedEffect(version) { techo.messages.markRead(peer) }
    LaunchedEffect(msgs.size) { if (msgs.isNotEmpty()) listState.animateScrollToItem(msgs.lastIndex) }

    Column(Modifier.fillMaxSize().imePadding()) {
        LazyColumn(
            Modifier.weight(1f).fillMaxWidth(),
            state = listState,
            contentPadding = androidx.compose.foundation.layout.PaddingValues(8.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            items(msgs, key = { it.rowId }) { Bubble(it) }
        }
        Row(Modifier.padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = draft,
                onValueChange = { draft = it.take(APRS_MSG_MAX) },
                modifier = Modifier.weight(1f),
                placeholder = { Text(L.s(R.string.msg_hint)) },
                supportingText = { Text("${draft.length}/$APRS_MSG_MAX") },
                maxLines = 3,
            )
            IconButton(
                enabled = draft.isNotBlank(),
                onClick = {
                    val text = draft.trim()
                    draft = ""
                    scope.launch {
                        val r = withContext(Dispatchers.Default) { techo.sendMessage(peer, text) }
                        r.onFailure { snack.showSnackbar(L.s(R.string.msg_not_sent, it.message ?: "")) }
                    }
                },
            ) { Icon(Icons.AutoMirrored.Filled.Send, L.s(R.string.send), tint = MaterialTheme.colorScheme.primary) }
        }
    }
}

@Composable
private fun Bubble(m: ChatMessage) {
    val mine = m.outgoing
    Row(Modifier.fillMaxWidth(), horizontalArrangement = if (mine) Arrangement.End else Arrangement.Start) {
        Surface(
            color = if (mine) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant,
            shape = RoundedCornerShape(
                topStart = 16.dp, topEnd = 16.dp, bottomStart = if (mine) 16.dp else 4.dp, bottomEnd = if (mine) 4.dp else 16.dp
            ),
            modifier = Modifier.widthIn(max = 300.dp),
        ) {
            Column(Modifier.padding(horizontal = 12.dp, vertical = 8.dp)) {
                Text(m.text)
                Row(Modifier.align(Alignment.End), verticalAlignment = Alignment.CenterVertically) {
                    Text(shortTime(m.time), style = MaterialTheme.typography.labelSmall)
                    if (mine) {
                        val (mark, color) = when (m.state) {
                            MsgState.SENDING -> "  ⏳" to Color.Unspecified
                            MsgState.SENT -> "  ✓" to Color.Unspecified
                            MsgState.ACKED -> "  ✓✓" to MaterialTheme.colorScheme.primary
                            MsgState.REJECTED -> "  ✗ " + L.s(R.string.msg_rejected) to MaterialTheme.colorScheme.error
                            MsgState.FAILED -> "  ✗ " + (m.note ?: L.s(R.string.msg_no_ack)) to MaterialTheme.colorScheme.error
                            MsgState.RECEIVED -> "" to Color.Unspecified
                        }
                        Text(mark, style = MaterialTheme.typography.labelSmall, color = color)
                    }
                }
            }
        }
    }
}

@Composable
private fun Avatar(call: String) {
    val hue = (call.hashCode() and 0xFFFF) % 360
    Box(
        Modifier.size(40.dp).background(Color.hsv(hue.toFloat(), 0.45f, 0.75f), CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        Text(call.filter { it.isLetterOrDigit() }.take(2), color = Color.White, fontWeight = FontWeight.Bold)
    }
}

private fun shortTime(ts: Long): String {
    val now = System.currentTimeMillis()
    val fmt = if (now - ts < 20 * 3600_000L) "HH:mm" else "dd/MM HH:mm"
    return SimpleDateFormat(fmt, Locale.getDefault()).format(Date(ts))
}
