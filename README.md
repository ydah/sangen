# 算言

算言者以漢文記算法之言也
源文綴以 `.kbn` 字用萬國碼第八式
處理系以丙語第九十九式成之

## 造

```sh
make
```

## 用

```sh
./sangen examples/fizzbuzz.kbn
./sangen examples/fizzbuzz.kbn --字碼
./sangen examples/fizzbuzz.kbn --詞
./sangen examples/fizzbuzz.kbn --文樹
./sangen examples/fizzbuzz.kbn --譯=c > fizzbuzz.c
./sangen examples/fizzbuzz.kbn --校
./sangen examples/fizzbuzz.kbn --正格
./sangen examples/fizzbuzz.kbn --整
```

## 法

注始於 `注曰` 終於 `注畢`
辭始於 `辭曰` 終於 `辭畢` 欲書 `辭畢` 則重之
數至 `九千九百九十九億九千九百九十九萬九千九百九十九`
二數相算曰 `甲乙之和` 亦可曰 `甲與乙之和`
比較曰 `甲大於乙` `甲小於乙` `甲等於乙` 又可曰 `甲與乙等`
除餘之辨曰 `以三除甲而無餘` `而` 可省
凡歷曰 `凡甲自一至百者 ... 焉` `者` 可省
術受天干 用時取同名之值
詞法不遽以漢字爲文法之辭 隨文脈而讀 `若` `則` `術` 等
故 `大` `然` `歸` 等字亦得入術名
`--譯=c` 者譯算言爲丙語也
`--校` 者告舊法也 `--正格` 者不許舊法也
`--整` 者因文樹整出正格源文也

舊法如 `使甲為七` `方甲過零` `否則` `答` `而已矣` `術畢` `倍術` 亦受之

## 術

```kbn
夫倍者術也
受甲
歸甲二之積
已矣

令甲爲七
書用倍術
```

## 試

```sh
make test
```
