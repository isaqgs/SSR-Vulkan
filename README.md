# Screen-Space Reflections com Vulkan

Projeto acadêmico em C++17 que inicializa Vulkan e integra um pipeline de três etapas: renderização da cena, cálculo de Screen-Space Reflections (SSR) e composição da imagem final. Pressione **F1** para alternar entre a cena normal e a cena com SSR; o título da janela mostra os controles, a visualização F2 atual, o estado do SSR, FPS e tempo médio por frame.

O programa apresenta uma cena iluminada com chão quadriculado, três cubos e duas esferas suaves. O passe SSR usa cor, profundidade e normais da cena para traçar reflexos em espaço de tela, com refinamento da interseção.

### Controles durante a demonstração

- **F1**: liga/desliga SSR.
- **F2**: percorre a saída final, SceneColor, profundidade linearizada, normais, máscara de acertos SSR e mapa de distância dos raios.
- **F3**: comparação lado a lado; metade esquerda com SSR e metade direita sem SSR.
- **W/A/S/D**: movimenta a câmera pelo cenário, com colisão contra os objetos e os limites do chão.
- **Segurar o botão direito + mover o mouse**: olha ao redor; solte o botão para liberar o cursor.

A câmera se mantém na altura aproximada do meio do cubo vermelho. O movimento é no plano do chão, sem pulo ou gravidade.

Ao caminhar contra um objeto, é possível empurrá-lo: com o baixo atrito do chão, os cubos deslizam como no gelo e as esferas rolam com rotação visualmente enfatizada. A simulação usa colisores simples adequados à cena; não é uma simulação de corpos rígidos completa.

## Tecnologias

- C++17
- Vulkan (o programa solicita Vulkan 1.2)
- GLFW 3
- GLM
- CMake 3.20 ou superior
- `glslc`, incluído no Vulkan SDK, para compilar GLSL em SPIR-V

## Arquivos principais

- `src/main.cpp`: inicialização Vulkan, recursos, pipelines, sincronização, loop de renderização e limpeza.
- `shaders/scene.vert` e `shaders/scene.frag`: geometria da cena, cor e normais.
- `shaders/ssr.vert` e `shaders/ssr.frag`: passe fullscreen e cálculo SSR.
- `shaders/composite.vert` e `shaders/composite.frag`: composição da cena e do resultado SSR.
- `CMakeLists.txt`: dependências, build C++ e compilação automática dos seis shaders para SPIR-V.
- `Entrega_4_SSR.tex` e `Entrega_4_SSR.pdf`: documentação da entrega.

## Requisitos no Windows

1. Windows 10 ou 11 e uma GPU com suporte a Vulkan 1.2.
2. Visual Studio 2022 Build Tools ou Community, com o workload **Desktop development with C++**, MSVC e Windows SDK.
3. CMake 3.20 ou superior: https://cmake.org/download/
4. Vulkan SDK da LunarG: https://vulkan.lunarg.com/
5. Git: https://git-scm.com/download/win
6. vcpkg para instalar GLFW e GLM: https://github.com/microsoft/vcpkg

O VS Code pode ser usado como editor; ele não substitui o compilador C++ nem o CMake.

## Preparar as dependências

Abra um terminal PowerShell e instale GLFW e GLM pelo vcpkg. O exemplo abaixo usa `C:\vcpkg`; se escolher outro local, ajuste esse caminho nos comandos seguintes.

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
& C:\vcpkg\bootstrap-vcpkg.bat
& C:\vcpkg\vcpkg.exe install glfw3:x64-windows glm:x64-windows
```

Confira se CMake, `glslc` e o loader Vulkan estão disponíveis:

```powershell
cmake --version
glslc --version
vulkaninfo --summary
```

O `vulkaninfo` deve listar uma GPU com suporte a Vulkan. Se `glslc` não for encontrado, abra um novo terminal após instalar o Vulkan SDK ou adicione a pasta `Bin` do SDK ao `PATH`.

## Compilar e executar

No PowerShell, entre na pasta clonada do projeto, que contém o `CMakeLists.txt`:

```powershell
cd "C:\caminho\para\SSR_Entrega_4"
```

Configure o CMake usando o toolchain do vcpkg:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

Compile e execute a versão Debug:

```powershell
cmake --build build --config Debug --parallel
.\build\Debug\ssr_app.exe
```

Ou compile e execute a versão Release:

```powershell
cmake --build build --config Release --parallel
.\build\Release\ssr_app.exe
```

O CMake chama `glslc` automaticamente e grava os arquivos SPIR-V em `build/shaders/`. Não é necessário compilar os shaders manualmente. Em Debug, o programa habilita `VK_LAYER_KHRONOS_validation`; em Release, as camadas ficam desativadas.

Para verificar os SPIR-V, se o Vulkan SDK estiver instalado:

```powershell
Get-ChildItem build\shaders\*.spv | ForEach-Object {
    & "$env:VULKAN_SDK\Bin\spirv-val.exe" --target-env vulkan1.2 $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "SPIR-V inválido: $($_.Name)" }
}
```

## O que esperar

Ao executar, deve abrir uma janela com a cena e o SSR ativado. O título indica o estado do efeito e atualiza o FPS e o tempo médio por frame duas vezes por segundo. Use **F1** para alternar o SSR, **F2** para inspecionar os buffers intermediários ou **F3** para comparar SSR ligado e desligado simultaneamente. Redimensionar a janela recria a swapchain e seus recursos associados.

O pipeline SSR usa cor, profundidade e normais da cena para percorrer o raio refletido até 12 unidades, fazer até 144 amostras e refinar uma interseção encontrada com busca binária. A profundidade e as normais usam amostragem pontual para evitar misturar superfícies diferentes; a cor refletida usa amostragem linear. A composição aplica atenuação por distância, borda e Fresnel. Quando o SSR está desativado, a composição exibe somente a cena iluminada.

Os buffers de câmera e parâmetros são separados por frame em voo, permitindo atualizar a câmera sem sobrescrever dados ainda utilizados pela GPU. Ao redimensionar a janela, a swapchain e os descritores são recriados.

## Solução de problemas

- **`cmake` não é reconhecido:** instale CMake ou reabra o terminal após a instalação.
- **`glslc` não é encontrado:** instale o Vulkan SDK e confira o `PATH`.
- **GLFW ou GLM não são encontrados:** confirme que os pacotes foram instalados com o mesmo triplet `x64-windows` e que o comando CMake inclui o toolchain do vcpkg.
- **`VK_LAYER_KHRONOS_validation` não está disponível:** instale ou repare o Vulkan SDK. A camada é necessária para executar a versão Debug.
- **Nenhuma GPU Vulkan adequada é encontrada:** atualize o driver gráfico e execute `vulkaninfo --summary` para verificar o suporte do dispositivo.
- **Avisos mencionam arquivos de camada Steam ausentes:** são avisos do loader sobre camadas instaladas no sistema; não são, por si só, erros deste projeto.

## Licença e materiais da atividade

Este repositório não define uma licença de uso. Os materiais e as instruções da atividade acadêmica estão em `Entrega_4_SSR.tex` e `Entrega_4_SSR.pdf`.
