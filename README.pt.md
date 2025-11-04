# BootThatISO!

**[🇺🇸 English Version](README.md) | [🇪🇸 Versión en Español](README.es.md)**

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

BootThatISO! é uma ferramenta inovadora para Windows que permite **inicializar sistemas operacionais a partir de arquivos ISO sem precisar de um pendrive USB**. Ideal para situações em que você não tem um dispositivo USB à mão, como durante viagens, equipamentos emprestados ou emergências. Automatiza a criação de partições EFI e de dados no disco interno, leitura direta de ISO e extração de arquivos, e configuração BCD, oferecendo uma interface gráfica intuitiva e suporte para execução desassistida.

Este utilitário é especialmente útil para:
- **Instalações Rápidas**: Inicialização direta do ISO para instalação do Windows, Linux ou ferramentas de recuperação sem preparar USB.
- **Ambientes de Teste**: Teste ISOs de sistemas operacionais ou utilitários sem modificar hardware externo.
- **Recuperação de Sistema**: Acesse ferramentas de reparo como HBCD_PE ou ambientes live sem depender de mídia externa.
- **Automação**: Integração em scripts para implantações em massa ou configurações automatizadas.

Desenvolvido por **Andrey Rodríguez Araya**.

Website: [English](https://agsoft.co.cr/en/software-and-services/) | [Español](https://agsoft.co.cr/servicios/)

![Screenshot](screenshot.png?v=1)

![Boot screen](boot_screen.png?v=1)

## Recursos Principais
- Cria ou reforma partições `ISOBOOT` (dados) e `ISOEFI` (EFI) no disco do sistema, com opções de formato FAT32, exFAT ou NTFS.
- Suporta dois modos de inicialização: carregamento completo do ISO no disco ou modo RAMDisk (boot.wim na memória).
- Detecta ISOs do Windows e ajusta automaticamente a configuração BCD; ISOs não-Windows inicializam diretamente da partição EFI.
- Executa verificações opcionais de integridade (`chkdsk`), gera logs detalhados e permite cancelamento ou recuperação de espaço.
- Fornece modo desassistido para integrações de script via argumentos de linha de comando.
- **Cache de hash do ISO (ISOBOOTHASH)**: Compara o MD5 do ISO, modo de inicialização selecionado e formato contra valores armazenados no arquivo `ISOBOOTHASH` no destino. Se corresponderem, ignora a formatação e cópia de conteúdo para acelerar execuções repetidas.

## ISOs Testados

### Modo RAM (Inicialização da Memória)
- ✅ HBCD_PE_x64.iso (TOTALMENTE FUNCIONAL - Carrega todos os programas da RAM)
- ✅ Win11_25H2_Spanish_x64.iso (TOTALMENTE FUNCIONAL - Inicialização e Instalação)
- ✅ Windows10_22H2_X64.iso (TOTALMENTE FUNCIONAL - Inicialização e Instalação)

### Modo EXTRACT (Instalação Completa)
- ✅ HBCD_PE_x64.iso (volta para ISOBOOT_RAM)
- ✅ Win11_25H2_Spanish_x64.iso (volta para ISOBOOT_RAM)
- ✅ Windows10_22H2_X64.iso (volta para ISOBOOT_RAM)

## Requisitos
- Windows 10 ou 11 64-bit com privilégios de administrador.
- Pelo menos 12 GB de espaço livre na unidade `C:` para criar e formatar partições (a ferramenta tenta reduzir 12 GB).
- PowerShell, DiskPart, bcdedit e ferramentas de linha de comando do Windows disponíveis.
- Para compilação: Visual Studio 2022 com CMake. Nenhum gerenciador de pacotes externo necessário; o SDK 7‑Zip está incluído em `third-party/`.

## Compilação
```powershell
# Configurar e compilar (VS 2022, x64)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

O executável final está localizado em `build/Release/BootThatISO!.exe`. Também está incluído `compilar.bat` com etapas equivalentes.

### Compilação rápida com compilar.bat (recomendado)
```powershell
# Na raiz do repositório
./compilar.bat
```

#### Assinatura de código
- Para pular a assinatura (útil em máquinas de desenvolvimento sem certificado):
```powershell
$env:SIGN_CERT_SHA1 = "skip"
./compilar.bat
```
- Para assinar, defina `SIGN_CERT_SHA1` para o SHA1 thumbprint do seu certificado ou certifique-se de que `signtool.exe` possa encontrar um certificado adequado em seu armazenamento.

#### Formatação de código
O script `compilar.bat` formata automaticamente todo o código-fonte usando `clang-format` antes de compilar:
- Formata todos os arquivos `.cpp` e `.h` nos diretórios `src/`, `include/` e `tests/`
- Usa o arquivo de configuração `.clang-format` do projeto
- Se `clang-format` não for encontrado, um aviso é exibido, mas a compilação continua

Nota: O projeto agora usa o SDK 7‑Zip (incluído) para leitura/extração de ISO; vcpkg ou libarchive não são necessários.

### Notas de compilação
- Binários Release são vinculados com o runtime MSVC estático (/MT) para um EXE autocontido (não requer VC++ Redistributable).
- O SDK 7‑Zip é compilado como uma biblioteca estática e vinculado como whole-archive para manter o registro de manipuladores.
- Manipuladores incluídos: UDF, ISO, contêiner Ext e MBR. O leitor prefere UDF e pode desembrulhar Ext para alcançar o fluxo UDF/ISO interno.

### Diagnósticos e testes
Os seguintes utilitários de console são construídos junto com o aplicativo para validar o comportamento:

```powershell
# Listar manipuladores suportados e tentar abrir via UDF/ISO
build/Release/ListFormats.exe

# Listar conteúdo do ISO e extrair automaticamente todos *.wim/*.esd para %TEMP%\EasyISOBoot_iso_extract_test
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\Win11_25H2_Spanish_x64.iso"
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\Windows10_22H2_X64.iso"
build/Release/TestISOReader.exe "C:\Users\Andrey\Documentos\EasyISOBoot\isos\HBCD_PE_x64.iso"

# Detectar heurística de ISO do Windows
build/Release/TestISODetection.exe "C:\caminho\para\algum.iso"

# Demonstração de recuperação de espaço (usa as mesmas rotinas do aplicativo)
build/Release/TestRecoverSpace.exe

# Demo de substituição de INI
build/Release/test_ini_replacer.exe
```

Notas:
- O teste preserva caminhos internos do ISO ao extrair (por exemplo, escreve em %TEMP%\EasyISOBoot_iso_extract_test\sources\boot.wim).
- ISOs do Windows podem usar `install.wim` ou `install.esd`; o teste descobre e extrai todos os arquivos `.wim` e `.esd` que encontra.
- ISOs híbridos do Windows expõem poucos itens através do manipulador ISO; abrir através do UDF produz a lista completa de arquivos (manipulado automaticamente).

Executar testes unitários com CTest:
```powershell
cd build
ctest -C Release --output-on-failure
```

### Validação de cópia da imagem de instalação
- Ao copiar a imagem do Windows do ISO (`sources/install.wim` ou `sources/install.esd`), o aplicativo agora verifica:
  - Correspondência de tamanho: compara o tamanho dentro do ISO com o tamanho do arquivo extraído no disco.
  - Integridade da imagem: executa `DISM /Get-WimInfo /WimFile:"<dest>"` e verifica índices válidos.
- Os resultados são gravados em `logs/iso_extract_log.log` e mostrados na UI. Qualquer incompatibilidade ou falha do DISM é sinalizada para que você possa tentar novamente a extração.

## Uso
### Interface Gráfica
1. Execute `BootThatISO!.exe` **como administrador** (o manifesto já solicita).
2. Selecione o arquivo ISO e escolha o formato do sistema de arquivos para `ISOBOOT`.
3. Defina o modo de inicialização:
   - `Instalação Completa`: copia todo o conteúdo do ISO para o disco.
   - `Iniciar da Memória`: copia `boot.wim` e dependências para inicializar da RAM.
4. Decida se deseja executar `chkdsk` (desmarcado pula a verificação para acelerar o processo).
5. Clique em **Criar Partição Inicializável** e monitore o progresso através da barra principal, barra detalhada e painel de log.
6. Após a conclusão, uma confirmação de reinicialização aparecerá. Use o botão **Recuperar Espaço** se precisar remover as partições `ISOBOOT`/`ISOEFI` e estender `C:`.
7. O botão **Serviços** abre a página de suporte `https://agsoft.co.cr/servicios/`.

### Modo Desassistido
Execute o binário com privilégios elevados e os seguintes argumentos:

```
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\caminho\imagem.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr|pt_br
```

- `-mode=RAM` ativa o modo *Iniciar da Memória* e copia `boot.wim`/`boot.sdi`.
- `-mode=EXTRACT` corresponde à *Instalação Completa*.
- `-chkdsk=TRUE` força a verificação do disco (omitido por padrão).
- `-lang` define o código de idioma correspondente aos arquivos em `lang/`.
- `-autoreboot` está disponível para automações futuras; atualmente apenas registra a preferência.

O processo registra eventos e sai sem mostrar a janela principal.

## Resumo do Fluxo Interno
1. **Validação e Partições** (`PartitionManager`): verifica o espaço disponível, executa `chkdsk` opcional, reduz `C:` em ~10.5 GB, cria `ISOEFI` (1024 MB FAT32) e `ISOBOOT` (10 GB), ou reforma os existentes, e expõe métodos de recuperação.
2. **Preparação de Conteúdo** (`ISOCopyManager`): lê o conteúdo do ISO usando o SDK 7‑Zip (manipulador ISO), classifica se é Windows, lista o conteúdo, copia arquivos para as unidades de destino e delega o gerenciamento EFI para `EFIManager`.
3. **Processamento de Inicialização** (`BootWimProcessor`): orquestra a extração e processamento de boot.wim, coordena com módulos especializados:
   - `WimMounter`: gerencia operações DISM para montar/desmontar arquivos WIM
   - `DriverIntegrator`: integra drivers do sistema e personalizados na imagem WIM
   - `PecmdConfigurator`: configura ambientes Hiren's BootCD PE
   - `StartnetConfigurator`: configura ambientes WinPE padrão
   - `IniFileProcessor`: processa arquivos INI com substituição de letra de unidade
   - `ProgramsIntegrator`: integra programas adicionais no ambiente de inicialização
4. **Cópia e Progresso** (`FileCopyManager`/`EventManager`): notifica o progresso granular, permite cancelamento e atualiza logs.
5. **Configuração BCD** (`BCDManager` + estratégias): cria entradas WinPE (RAMDisk) ou instalação completa, ajusta `{ramdiskoptions}` e registra comandos executados.
6. **UI Win32** (`MainWindow`): constrói controles manualmente, aplica estilo, manipula comandos e expõe opções de recuperação.

### Arquitetura Modular
O projeto segue uma arquitetura modular limpa com clara separação de preocupações:
- **Padrão Facade**: `BootWimProcessor` fornece uma interface simples para operações complexas de processamento de inicialização
- **Padrão Strategy**: A integração de drivers usa categorias (Storage, USB, Network) para configuração flexível
- **Padrão Observer**: `EventManager` notifica múltiplos observadores (UI, Logger) de atualizações de progresso
- **Chain of Responsibility**: A integração de programas tenta múltiplas estratégias de fallback
- **Responsabilidade Única**: Cada classe tem um propósito claro (montar WIMs, integrar drivers, etc.)

Para documentação detalhada da arquitetura, consulte `ARCHITECTURE.md`.

## Logs e Diagnósticos
Todas as operações geram arquivos em `logs/` (criados ao lado do executável). Entre os mais relevantes:
- `general_log.log`: linha do tempo geral de eventos e mensagens da UI.
- `diskpart_log.log`, `reformat_log.log`, `recover_script_log.txt`: etapas de particionamento e reformatação.
- `iso_extract_log.log`, `iso_content.log`: detalhes do conteúdo extraído do ISO.
- `bcd_config_log.log`: comandos e resultados de configuração BCD.
- `copy_error_log.log`, `iso_file_copy_log.log`: cópia de arquivos e erros.

Revise esses logs ao diagnosticar falhas ou compartilhar relatórios.

## Segurança e Recuperação
- A operação modifica o disco do sistema; faça backup antes de executar a ferramenta.
- Durante o processo, evite fechar o aplicativo pelo Gerenciador de Tarefas; use a opção de cancelar integrada.
- Use o botão **Recuperar Espaço** para remover as partições `ISOBOOT`/`ISOEFI` e restaurar a unidade `C:` se decidir reverter a configuração.

## Limitações
- Opera no Disco 0 e reduz o volume C: em ~10.5 GB; outros layouts de disco não são suportados atualmente.
- Requer privilégios de administrador e disponibilidade do Windows PowerShell.
- Arquivos de idioma em `lang/` são necessários; o aplicativo mostra um erro se nenhum for encontrado.

## Créditos
Desenvolvido por **Andrey Rodríguez Araya** em 2025.

## Licença
Este projeto está sob a Licença GPL 3.0. Consulte o arquivo `LICENSE` para mais detalhes.

## Avisos de terceiros
- SDK 7‑Zip: Partes deste produto incluem código do SDK 7‑Zip de Igor Pavlov.
  - Resumo de licenciamento (por `third-party/DOC/License.txt`):
    - A maioria dos arquivos está licenciada sob GNU LGPL (v2.1 ou posterior).
    - Alguns arquivos são de domínio público onde explicitamente declarado nos cabeçalhos.
    - `CPP/7zip/Compress/LzfseDecoder.cpp` está sob a licença BSD 3‑Clause.
    - `CPP/7zip/Compress/Rar*` estão sob GNU LGPL com a "restrição de licença unRAR".
  - Incluímos um subconjunto mínimo (manipulador ISO e utilitários comuns). Nenhum código RAR é usado por este projeto.
  - Textos completos: consulte `third-party/DOC/License.txt`, `third-party/DOC/lzma.txt` e `third-party/DOC/unRarLicense.txt`.
