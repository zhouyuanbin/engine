package io.flutter.plugin.editing;

import android.content.Context;
import android.content.res.AssetManager;
import android.os.Build;
import android.provider.Settings;
import android.util.SparseIntArray;
import android.view.inputmethod.CursorAnchorInfo;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputMethodSubtype;
import android.view.KeyEvent;
import android.view.View;

import java.nio.ByteBuffer;

import org.junit.runner.RunWith;
import org.junit.Test;

import org.json.JSONArray;
import org.json.JSONException;

import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.shadow.api.Shadow;
import org.robolectric.shadows.ShadowBuild;
import org.robolectric.shadows.ShadowInputMethodManager;

import org.mockito.ArgumentCaptor;

import io.flutter.embedding.engine.dart.DartExecutor;
import io.flutter.embedding.engine.FlutterJNI;
import io.flutter.embedding.engine.systemchannels.TextInputChannel;
import io.flutter.plugin.common.BinaryMessenger;
import io.flutter.plugin.common.JSONMethodCodec;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.platform.PlatformViewsController;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

@Config(manifest = Config.NONE, shadows = TextInputPluginTest.TestImm.class, sdk = 27)
@RunWith(RobolectricTestRunner.class)
public class TextInputPluginTest {
    // Verifies the method and arguments for a captured method call.
    private void verifyMethodCall(ByteBuffer buffer, String methodName, String[] expectedArgs) throws JSONException {
        buffer.rewind();
        MethodCall methodCall = JSONMethodCodec.INSTANCE.decodeMethodCall(buffer);
        assertEquals(methodName, methodCall.method);
        if (expectedArgs != null) {
            JSONArray args = methodCall.arguments();
            assertEquals(expectedArgs.length, args.length());
            for (int i = 0; i < args.length(); i++) {
                assertEquals(expectedArgs[i], args.get(i).toString());
            }
        }
    }

    @Test
    public void textInputPlugin_RequestsReattachOnCreation() throws JSONException {
        // Initialize a general TextInputPlugin.
        InputMethodSubtype inputMethodSubtype = mock(InputMethodSubtype.class);
        TestImm testImm = Shadow.extract(RuntimeEnvironment.application.getSystemService(Context.INPUT_METHOD_SERVICE));
        testImm.setCurrentInputMethodSubtype(inputMethodSubtype);
        View testView = new View(RuntimeEnvironment.application);

        FlutterJNI mockFlutterJni = mock(FlutterJNI.class);
        DartExecutor dartExecutor = spy(new DartExecutor(mockFlutterJni, mock(AssetManager.class)));
        TextInputPlugin textInputPlugin = new TextInputPlugin(testView, dartExecutor, mock(PlatformViewsController.class));

        ArgumentCaptor<String> channelCaptor = ArgumentCaptor.forClass(String.class);
        ArgumentCaptor<ByteBuffer> bufferCaptor = ArgumentCaptor.forClass(ByteBuffer.class);

        verify(dartExecutor, times(1)).send(channelCaptor.capture(), bufferCaptor.capture(), any(BinaryMessenger.BinaryReply.class));
        assertEquals("flutter/textinput", channelCaptor.getValue());
        verifyMethodCall(bufferCaptor.getValue(), "TextInputClient.requestExistingInputState", null);
    }

    @Test
    public void setTextInputEditingState_doesNotRestartWhenTextIsIdentical() {
        // Initialize a general TextInputPlugin.
        InputMethodSubtype inputMethodSubtype = mock(InputMethodSubtype.class);
        TestImm testImm = Shadow.extract(RuntimeEnvironment.application.getSystemService(Context.INPUT_METHOD_SERVICE));
        testImm.setCurrentInputMethodSubtype(inputMethodSubtype);
        View testView = new View(RuntimeEnvironment.application);
        TextInputPlugin textInputPlugin = new TextInputPlugin(testView, mock(DartExecutor.class), mock(PlatformViewsController.class));
        textInputPlugin.setTextInputClient(0, new TextInputChannel.Configuration(false, false, true, TextInputChannel.TextCapitalization.NONE, null, null, null));
        // There's a pending restart since we initialized the text input client. Flush that now.
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));

        // Move the cursor.
        assertEquals(1, testImm.getRestartCount(testView));
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));

        // Verify that we haven't restarted the input.
        assertEquals(1, testImm.getRestartCount(testView));
    }

    @Test
    public void setTextInputEditingState_alwaysSetEditableWhenDifferent() {
        // Initialize a general TextInputPlugin.
        InputMethodSubtype inputMethodSubtype = mock(InputMethodSubtype.class);
        TestImm testImm = Shadow.extract(RuntimeEnvironment.application.getSystemService(Context.INPUT_METHOD_SERVICE));
        testImm.setCurrentInputMethodSubtype(inputMethodSubtype);
        View testView = new View(RuntimeEnvironment.application);
        TextInputPlugin textInputPlugin = new TextInputPlugin(testView, mock(DartExecutor.class), mock(PlatformViewsController.class));
        textInputPlugin.setTextInputClient(0, new TextInputChannel.Configuration(false, false, true, TextInputChannel.TextCapitalization.NONE, null, null, null));
        // There's a pending restart since we initialized the text input client. Flush that now. With changed text, we should
        // always set the Editable contents.
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("hello", 0, 0));
        assertEquals(1, testImm.getRestartCount(testView));
        assertTrue(textInputPlugin.getEditable().toString().equals("hello"));

        // No pending restart, set Editable contents anyways.
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("Shibuyawoo", 0, 0));
        assertEquals(1, testImm.getRestartCount(testView));
        assertTrue(textInputPlugin.getEditable().toString().equals("Shibuyawoo"));
    }

    // See https://github.com/flutter/flutter/issues/29341 and https://github.com/flutter/flutter/issues/31512
    // All modern Samsung keybords are affected including non-korean languages and thus
    // need the restart.
    @Test
    public void setTextInputEditingState_alwaysRestartsOnAffectedDevices2() {
        // Initialize a TextInputPlugin that needs to be always restarted.
        ShadowBuild.setManufacturer("samsung");
        InputMethodSubtype inputMethodSubtype = new InputMethodSubtype(0, 0, /*locale=*/"en", "", "", false, false);
        Settings.Secure.putString(RuntimeEnvironment.application.getContentResolver(), Settings.Secure.DEFAULT_INPUT_METHOD, "com.sec.android.inputmethod/.SamsungKeypad");
        TestImm testImm = Shadow.extract(RuntimeEnvironment.application.getSystemService(Context.INPUT_METHOD_SERVICE));
        testImm.setCurrentInputMethodSubtype(inputMethodSubtype);
        View testView = new View(RuntimeEnvironment.application);
        TextInputPlugin textInputPlugin = new TextInputPlugin(testView, mock(DartExecutor.class), mock(PlatformViewsController.class));
        textInputPlugin.setTextInputClient(0, new TextInputChannel.Configuration(false, false, true, TextInputChannel.TextCapitalization.NONE, null, null, null));
        // There's a pending restart since we initialized the text input client. Flush that now.
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));

        // Move the cursor.
        assertEquals(1, testImm.getRestartCount(testView));
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));

        // Verify that we've restarted the input.
        assertEquals(2, testImm.getRestartCount(testView));
    }

    @Test
    public void setTextInputEditingState_doesNotRestartOnUnaffectedDevices() {
        // Initialize a TextInputPlugin that needs to be always restarted.
        ShadowBuild.setManufacturer("samsung");
        InputMethodSubtype inputMethodSubtype = new InputMethodSubtype(0, 0, /*locale=*/"en", "", "", false, false);
        Settings.Secure.putString(RuntimeEnvironment.application.getContentResolver(), Settings.Secure.DEFAULT_INPUT_METHOD, "com.fake.test.blah/.NotTheRightKeyboard");
        TestImm testImm = Shadow.extract(RuntimeEnvironment.application.getSystemService(Context.INPUT_METHOD_SERVICE));
        testImm.setCurrentInputMethodSubtype(inputMethodSubtype);
        View testView = new View(RuntimeEnvironment.application);
        TextInputPlugin textInputPlugin = new TextInputPlugin(testView, mock(DartExecutor.class), mock(PlatformViewsController.class));
        textInputPlugin.setTextInputClient(0, new TextInputChannel.Configuration(false, false, true, TextInputChannel.TextCapitalization.NONE, null, null, null));
        // There's a pending restart since we initialized the text input client. Flush that now.
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));

        // Move the cursor.
        assertEquals(1, testImm.getRestartCount(testView));
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));

        // Verify that we've restarted the input.
        assertEquals(1, testImm.getRestartCount(testView));
    }

    @Test
    public void setTextInputEditingState_nullInputMethodSubtype() {
        TestImm testImm = Shadow.extract(RuntimeEnvironment.application.getSystemService(Context.INPUT_METHOD_SERVICE));
        testImm.setCurrentInputMethodSubtype(null);

        View testView = new View(RuntimeEnvironment.application);
        TextInputPlugin textInputPlugin = new TextInputPlugin(testView, mock(DartExecutor.class), mock(PlatformViewsController.class));
        textInputPlugin.setTextInputClient(0, new TextInputChannel.Configuration(false, false, true, TextInputChannel.TextCapitalization.NONE, null, null, null));
        // There's a pending restart since we initialized the text input client. Flush that now.
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));
        assertEquals(1, testImm.getRestartCount(testView));
    }

    @Test
    public void inputConnection_createsActionFromEnter() throws JSONException {
        TestImm testImm = Shadow.extract(RuntimeEnvironment.application.getSystemService(Context.INPUT_METHOD_SERVICE));
        FlutterJNI mockFlutterJni = mock(FlutterJNI.class);
        View testView = new View(RuntimeEnvironment.application);
        DartExecutor dartExecutor = spy(new DartExecutor(mockFlutterJni, mock(AssetManager.class)));
        TextInputPlugin textInputPlugin = new TextInputPlugin(testView, dartExecutor, mock(PlatformViewsController.class));
        textInputPlugin.setTextInputClient(
            0,
            new TextInputChannel.Configuration(
                false, false, true, TextInputChannel.TextCapitalization.NONE,
                new TextInputChannel.InputType(TextInputChannel.TextInputType.TEXT, false, false), null, null));
        // There's a pending restart since we initialized the text input client. Flush that now.
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));

        ArgumentCaptor<String> channelCaptor = ArgumentCaptor.forClass(String.class);
        ArgumentCaptor<ByteBuffer> bufferCaptor = ArgumentCaptor.forClass(ByteBuffer.class);
        verify(dartExecutor, times(1)).send(channelCaptor.capture(), bufferCaptor.capture(), any(BinaryMessenger.BinaryReply.class));
        assertEquals("flutter/textinput", channelCaptor.getValue());
        verifyMethodCall(bufferCaptor.getValue(), "TextInputClient.requestExistingInputState", null);
        InputConnection connection = textInputPlugin.createInputConnection(testView, new EditorInfo());

        connection.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER));
        verify(dartExecutor, times(2)).send(channelCaptor.capture(), bufferCaptor.capture(), any(BinaryMessenger.BinaryReply.class));
        assertEquals("flutter/textinput", channelCaptor.getValue());
        verifyMethodCall(bufferCaptor.getValue(), "TextInputClient.performAction", new String[] {"0", "TextInputAction.done"});
        connection.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));

        connection.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_NUMPAD_ENTER));
        verify(dartExecutor, times(3)).send(channelCaptor.capture(), bufferCaptor.capture(), any(BinaryMessenger.BinaryReply.class));
        assertEquals("flutter/textinput", channelCaptor.getValue());
        verifyMethodCall(bufferCaptor.getValue(), "TextInputClient.performAction", new String[] {"0", "TextInputAction.done"});
    }

    @Test
    public void inputConnection_finishComposingTextUpdatesIMM() throws JSONException {
        TestImm testImm = Shadow.extract(RuntimeEnvironment.application.getSystemService(Context.INPUT_METHOD_SERVICE));
        FlutterJNI mockFlutterJni = mock(FlutterJNI.class);
        View testView = new View(RuntimeEnvironment.application);
        DartExecutor dartExecutor = spy(new DartExecutor(mockFlutterJni, mock(AssetManager.class)));
        TextInputPlugin textInputPlugin = new TextInputPlugin(testView, dartExecutor, mock(PlatformViewsController.class));
        textInputPlugin.setTextInputClient(
            0,
            new TextInputChannel.Configuration(
                false, false, true, TextInputChannel.TextCapitalization.NONE,
                new TextInputChannel.InputType(TextInputChannel.TextInputType.TEXT, false, false), null, null));
        // There's a pending restart since we initialized the text input client. Flush that now.
        textInputPlugin.setTextInputEditingState(testView, new TextInputChannel.TextEditState("", 0, 0));
        InputConnection connection = textInputPlugin.createInputConnection(testView, new EditorInfo());

        connection.finishComposingText();

        if (Build.VERSION.SDK_INT >= 21) {
            CursorAnchorInfo.Builder builder = new CursorAnchorInfo.Builder();
            builder.setComposingText(-1, "");
            CursorAnchorInfo anchorInfo = builder.build();
            assertEquals(testImm.getLastCursorAnchorInfo(), anchorInfo);
        }
    }

    @Implements(InputMethodManager.class)
    public static class TestImm extends ShadowInputMethodManager {
        private InputMethodSubtype currentInputMethodSubtype;
        private SparseIntArray restartCounter = new SparseIntArray();
        private CursorAnchorInfo cursorAnchorInfo;

        public TestImm() {
        }

        @Implementation
        public InputMethodSubtype getCurrentInputMethodSubtype() {
            return currentInputMethodSubtype;
        }

        @Implementation
        public void restartInput(View view) {
            int count = restartCounter.get(view.hashCode(), /*defaultValue=*/0) + 1;
            restartCounter.put(view.hashCode(), count);
        }

        public void setCurrentInputMethodSubtype(InputMethodSubtype inputMethodSubtype) {
            this.currentInputMethodSubtype = inputMethodSubtype;
        }

        public int getRestartCount(View view) {
            return restartCounter.get(view.hashCode(), /*defaultValue=*/0);
        }

        @Implementation
        public void updateCursorAnchorInfo(View view, CursorAnchorInfo cursorAnchorInfo) {
            this.cursorAnchorInfo = cursorAnchorInfo;
        }

        public CursorAnchorInfo getLastCursorAnchorInfo() {
            return cursorAnchorInfo;
        }
    }
}
