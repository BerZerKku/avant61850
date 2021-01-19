#!/bin/sh
LIBIEC61850_VERSION=v1.4-dev-041325ef
EXIT_SUCCESS=0
EXIT_FAILURE=1
EXIT_BACKUP=2

if [ $# -lt 2 ]; then
    echo "At least two arguments are required: ICD file (1) and output model file (2)"
    exit $EXIT_FAILURE
fi

ICD_FILE=$1
MODEL_FILE=$2
ICD_FILE_SHA1="$ICD_FILE.sha1"
MODEL_FILE_BKP="$MODEL_FILE~"

if [ ! -f $ICD_FILE ]; then
    echo "File $ICD_FILE does not exist"
    exit $EXIT_FAILURE
fi

GEN_NEW_MODEL_F=0
BKP_OLD_MODEL_F=0

if [ ! -f $ICD_FILE_SHA1 ]; then
    GEN_NEW_MODEL_F=1
else
    STAT=$(sha1sum -c $ICD_FILE_SHA1 | awk '{print $2}')
    if [ "$STAT" != "OK" ]; then
        GEN_NEW_MODEL_F=1
    fi
fi

if [ ! -f $MODEL_FILE ]; then
    GEN_NEW_MODEL_F=1
else
    MODEL_FILE_SIZE=$(stat -c%s "$MODEL_FILE")
    if [ $MODEL_FILE_SIZE -eq 0 ]; then
        GEN_NEW_MODEL_F=1
    fi
fi

if [ $GEN_NEW_MODEL_F -eq 0 ]; then
    # Генерировать новую модель не требуется
    exit $EXIT_SUCCESS
else
    # Удаляем старый SHA1 файл
    rm -f $ICD_FILE_SHA1
fi

# Создадим backup сущуствующей модели
if [ -f $MODEL_FILE ]; then
    MODEL_FILE_SIZE=$(stat -c%s "$MODEL_FILE")
    if [ ! $MODEL_FILE_SIZE -eq 0 ]; then
        # Файл модели существует и он не пустой
        BKP_OLD_MODEL_F=1
        mv $MODEL_FILE $MODEL_FILE_BKP
    fi
fi

java -jar tools/libiec61850/$LIBIEC61850_VERSION/genconfig.jar $@

MODEL_FILE_SIZE=0

if [ -f $MODEL_FILE ]; then
    # была создана новая модель
    MODEL_FILE_SIZE=$(stat -c%s "$MODEL_FILE")
fi

if [ $MODEL_FILE_SIZE -eq 0 ]; then
    # Если был создан пустой файл, он должен быть удален в любом случае
    rm -f $MODEL_FILE
    if [ $BKP_OLD_MODEL_F -eq 1 ]; then
        # Восстанавливаем старую модель
        mv $MODEL_FILE_BKP $MODEL_FILE
        exit $EXIT_BACKUP
    fi
    # Удалить backup, даже, если он был создан не в этот раз
    rm -f $MODEL_FILE_BKP
    exit $EXIT_FAILURE
fi

sha1sum $ICD_FILE > $ICD_FILE_SHA1
# Удалить backup, если существует
rm -f $MODEL_FILE_BKP
exit $EXIT_SUCCESS