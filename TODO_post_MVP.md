# TODO (post-MVP optimizations)

1. Dict threshold tuning
   - Aktuálne accepts all gain > 0 → 4071 entries
   - Pri zvýšení threshold na gain > 100 by zostalo ~500-1000 entries
   - Menej dict overhead, ale viac bytových kódov
   - Otestovať: pre demo_corpus by 1000 entries dali ratio ~78%?

2. Variable bit width per level (HIER s rôznymi base_size)
   - Aktuálne HIER8 má krok 6 bitov vždy
   - Variant: L0 = 8×8 (6 bitov), L1 = 16×16 (8 bitov)
   - Lepšie pokrytie zriedkavých symbolov

3. Huffman ako alternatívna stratégia
   - loxc_strategy_select už porovnáva FLAT/HIER8/HIER4
   - Pridať HUFFMAN ako 4. možnosť
   - Pri špicatých distribúciách by vyhrala 30-40% lepšie

4. Aritmetické kódovanie
   - Najpokročilejšia stratégia, ~50% lepšia ako Huffman
   - Komplikovaná implementácia
   - Premýšľať len ak loxc projekt naozaj bude potrebovať max kompresiu

