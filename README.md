# Variable Sized Bin Packing Problem (VSBPP) Solver

## Descrição do Projeto

Este projeto implementa um conjunto de algoritmos para resolver o Problema de Empacotamento em Contentores de Tamanhos Variados (Variable Sized Bin Packing Problem - VSBPP). O VSBPP é uma generalização do problema clássico de Bin Packing, onde existem múltiplos tipos de contentores (bins) com diferentes capacidades e custos. O objetivo é alocar todos os itens nos contentores de forma a minimizar o custo total.

## Estrutura do Código

O código está organizado da seguinte forma:

### Classe Principal:

- `VSBPP`: Classe que representa uma instância do problema, contendo:
  - Métodos para leitura de instâncias a partir de arquivos
  - Exibição de informações da instância

### Heurísticas Construtivas:

1. **Best Fit Decreasing (BFD)**: Ordena os itens em ordem decrescente de peso e os insere no primeiro contentor onde eles se encaixem melhor (deixando menos espaço residual).

2. **Subset Sum Problem (SSP)**: Heurística baseada no problema da soma de subconjuntos, considerada uma das mais eficazes para o VSBPP segundo a literatura. Para cada tipo de contentor, encontra o melhor subconjunto de itens que maximiza a utilização, calculando a razão custo/utilização.

### Heurísticas de Busca Local:

1. **Troca de Itens (swap_items)**: Tenta trocar itens entre pares de contentores para reduzir o custo total.

2. **Mudança de Tipo de Contentor (change_bin_type)**: Tenta mudar o tipo de cada contentor para um tipo menor e mais barato que ainda possa acomodar todos os itens.

3. **Redistribuição de Itens (redistribute_items)**: Tenta eliminar contentores redistribuindo seus itens para outros contentores existentes.

### Meta-heurísticas:

1. **Variable Neighborhood Descent (VND)**: Explora sistematicamente diferentes estruturas de vizinhança (as três buscas locais acima) para encontrar melhores soluções.

### Funções Auxiliares:

- **Knapsack**: Implementação do algoritmo de programação dinâmica para resolver o problema da mochila.
- **Verificação de Solução**: Verifica se uma solução é válida.
- **Cálculo de Custos**: Calcula o custo total de uma solução.
- **Exportação de Resultados**: Exporta os resultados para um arquivo CSV.

## Detalhes Técnicos da Implementação

### Estruturas de Dados

- **Representação de Instância**: 
  - `num_items`: Número de itens
  - `num_bin_types`: Número de tipos de contentores
  - `bin_capacities`: Lista das capacidades de cada tipo de contentor
  - `bin_costs`: Lista dos custos de cada tipo de contentor
  - `item_weights`: Lista dos pesos dos itens

- **Representação de Solução**: 
  - `bins`: Lista de contentores, onde cada contentor é uma lista de índices dos itens
  - `bin_types`: Lista indicando o tipo de cada contentor usado
  - `total_cost`: Custo total da solução

### Algoritmos Principais

#### Best Fit Decreasing (BFD)

1. Ordena os itens em ordem decrescente de peso
2. Para cada item:
   - Tenta inseri-lo no contentor existente onde ele se encaixe melhor (menor espaço residual)
   - Se não for possível, cria um novo contentor do menor tipo possível

#### Subset Sum Problem (SSP)

1. Cria uma lista de itens restantes
2. Enquanto houver itens:
   - Para cada tipo de contentor, resolve um problema da mochila (knapsack) para encontrar o melhor conjunto de itens
   - Escolhe o tipo de contentor com a melhor razão custo/utilização
   - Remove os itens selecionados da lista de itens restantes

#### Variable Neighborhood Descent (VND)

1. Inicializa com a melhor solução construtiva (BFD ou SSP)
2. Define as estruturas de vizinhança: troca de itens, mudança de tipo, redistribuição
3. Começa com a primeira vizinhança (k = 0)
4. Enquanto k < número de vizinhanças:
   - Aplica a busca local na vizinhança k
   - Se encontrar uma solução melhor, atualiza a solução atual e volta para k = 0
   - Caso contrário, passa para a próxima vizinhança (k = k + 1)

### Otimizações Implementadas

1. **Busca Eficiente de Tipo de Contentor**: Função `find_smallest_bin_type` para encontrar rapidamente o menor tipo de contentor que pode acomodar um peso.

2. **Programação Dinâmica para Knapsack**: Implementação eficiente do algoritmo de programação dinâmica para resolver o problema da mochila.

3. **Verificação Incremental**: Nas buscas locais, as mudanças são verificadas incrementalmente sem recalcular toda a solução.

4. **Gerenciamento de Tempo**: Limite de tempo configurável para cada instância.

5. **Detecção de Melhoria**: Parada antecipada das buscas locais quando não há mais melhorias possíveis.

## Complexidade Computacional

- **BFD**: O(n log n + n * m), onde n é o número de itens e m é o número de contentores usados.
- **SSP**: O(n * m * C), onde C é a maior capacidade de contentor.
- **Troca de Itens**: O(m² * b²), onde b é o número médio de itens por contentor.
- **Mudança de Tipo**: O(m * t), onde t é o número de tipos de contentores.
- **Redistribuição**: O(m² * n), no pior caso.
- **VND**: Depende do número de iterações necessárias para convergir.

## Formato das Instâncias

As instâncias são esperadas em arquivos de texto com o seguinte formato:

1. Primeira linha: `n m id`
   - `n`: Número de itens
   - `m`: Número de tipos de contentores
   - `id`: Identificador da instância (opcional)

2. Segunda linha: Capacidades e custos dos contentores
   - Formato intercalado: `capacidade1 custo1 capacidade2 custo2 ...`
   - Ou em blocos: `capacidade1 capacidade2 ... custo1 custo2 ...`

3. Linhas seguintes: Pesos dos itens, separados por espaços

## Resultados e Saída

O programa exibe informações detalhadas sobre:

1. Características da instância
2. Solução inicial encontrada (BFD ou SSP)
3. Solução final após a aplicação do VND
4. Melhoria obtida
5. Tempo de execução
6. Distribuição de tipos de contentores usados

Os resultados também são exportados para um arquivo CSV para análise posterior.
