import * as path from 'path';
import { workspace, ExtensionContext } from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: ExtensionContext) {
    // claw-lsp is a native executable, not a Node.js module
    const serverCommand = context.asAbsolutePath(
        path.join('..', '..', 'claw-lsp')
    );

    const serverOptions: ServerOptions = {
        run: { command: serverCommand, transport: TransportKind.stdio },
        debug: { command: serverCommand, transport: TransportKind.stdio }
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'claw' }],
        synchronize: {
            fileEvents: workspace.createFileSystemWatcher('**/*.claw')
        }
    };

    client = new LanguageClient(
        'clawLanguageServer',
        'Claw Language Server',
        serverOptions,
        clientOptions
    );

    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
